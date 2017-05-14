// =============================================================================
//  AMS - Advanced Module System - Common Library
// -----------------------------------------------------------------------------
//     Copyright (C) 2004,2005,2008 Petr Kulhanek (kulhanek@chemi.muni.cz)
//
//     This library is free software; you can redistribute it and/or
//     modify it under the terms of the GNU Lesser General Public
//     License as published by the Free Software Foundation; either
//     version 2.1 of the License, or (at your option) any later version.
//
//     This library is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//     Lesser General Public License for more details.
//
//     You should have received a copy of the GNU Lesser General Public
//     License along with this library; if not, write to the Free Software
//     Foundation, Inc., 51 Franklin Street, Fifth Floor,
//     Boston, MA  02110-1301  USA
// =============================================================================

#include <XMLParser.hpp>
#include <ErrorSystem.hpp>
#include <FileName.hpp>
#include <Shell.hpp>
#include <Utils.hpp>
#include "prefix.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fnmatch.h>
#include "AMSStatServer.hpp"
#include <FirebirdExecuteSQL.hpp>
#include <FirebirdQuerySQL.hpp>
#include <FirebirdItem.hpp>
#include <XMLIterator.hpp>
#include <signal.h>

//------------------------------------------------------------------------------

#define MAX_NET_NAME 255

using namespace std;

//------------------------------------------------------------------------------

CAMSStatServer Server;

MAIN_ENTRY_OBJECT(Server)

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================

CAMSStatServer::CAMSStatServer(void)
{
    Terminated = false;
}

//------------------------------------------------------------------------------

CAMSStatServer::~CAMSStatServer(void)
{
    if( Database.IsLogged() ) Database.Logout();
}

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================

int CAMSStatServer::Init(int argc, char* argv[])
{
    // encode program options, all check procedures are done inside of CABFIntOpts
    int result = Options.ParseCmdLine(argc,argv);

    // should we exit or was it error?
    if( result != SO_CONTINUE ) return(result);

    // stdout is used for shell processor
    Console.Attach(stdout);

    // attach verbose stream to terminal stream and set desired verbosity level
    vout.Attach(Console);
    if( Options.GetOptVerbose() ) {
        vout.Verbosity(CVerboseStr::high);
    } else {
        vout.Verbosity(CVerboseStr::low);
    }

    CSmallTimeAndDate dt;
    dt.GetActualTimeAndDate();

    vout << low;
    vout << endl;
    vout << "# ==============================================================================" << endl;
    vout << "# ams-isoftstat (AMS utility) started at " << dt.GetSDateAndTime() << endl;
    vout << "# ==============================================================================" << endl;

    // load config
    if( LoadConfig() == false ) {
        ES_ERROR("unable to load server config");
        return(SO_USER_ERROR);
    }

    // print header --------------------------------------------------------------
    vout << "# Server setup" << endl;
    vout << "# ----------------------------------" << endl;
    vout << "# Port        : " << GetPortNumber() << endl;
    vout << "#" << endl;
    vout << "# Statistics database" << endl;
    vout << "# ----------------------------------" << endl;
    vout << "# Database    : " << Server.GetDatabaseName() << endl;
    vout << "# User        : " << Server.GetDatabaseUser() << endl;
    vout << "# Password    : " << "********" << endl;
    vout << "#" << endl;
    CXMLElement* p_watcher = ServerConfig.GetChildElementByPath("config/watcher");
    Watcher.ProcessWatcherControl(vout,p_watcher);

    vout << "# ------------------------------------------------------------------------------" << endl;
    vout << endl;

    return(SO_CONTINUE);
}

//------------------------------------------------------------------------------

bool CAMSStatServer::Run(void)
{
    // init server
    if( InitServer() == false ) {
        ES_ERROR("unable to init server");
        return(false);
    }

    // set SIGINT hadler to cleanly shutdown server ----------
    signal(SIGINT,CtrlCSignalHandler);
    signal(SIGTERM,CtrlCSignalHandler);

    // execute server
    Watcher.StartThread(); // watcher
    if( ExecuteServer() == false ) {
        ES_ERROR("unable to execute server");
        return(false);
    }

    // watcher
    Watcher.TerminateThread();
    Watcher.WaitForThread();

    return(true);
}

//------------------------------------------------------------------------------

void CAMSStatServer::CtrlCSignalHandler(int signal)
{
    printf("\nCtrl+C signal recieved.\n   Initiating server shutdown ... \n");
    printf("Waiting for server finalization ... \n");
    fflush(stdout);
    Server.ShutdownServer();
}

//------------------------------------------------------------------------------

void CAMSStatServer::Finalize(void)
{
    CSmallTimeAndDate dt;
    dt.GetActualTimeAndDate();

    vout << low;
    vout << endl;
    vout << "# ==============================================================================" << endl;
    vout << "# ams-isoftstat (AMS utility) terminated at " << dt.GetSDateAndTime() << endl;
    vout << "# ==============================================================================" << endl;

    if( ErrorSystem.IsError() || Options.GetOptVerbose() ){
        ErrorSystem.PrintErrors(vout);
    }

    vout << endl;
}

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================

bool CAMSStatServer::LoadConfig(void)
{
    CFileName    config_path;

    config_path = CFileName(ETCDIR) / "servers" / "stat.xml";

    CXMLParser xml_parser;
    xml_parser.SetOutputXMLNode(&ServerConfig);

    if( xml_parser.Parse(config_path) == false ) {
        CSmallString error;
        error << "unable to load server config";
        ES_ERROR(error);
        return(false);
    }

    return(true);
}

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================

bool CAMSStatServer::InitServer(void)
{
    Database.SetDatabaseName(GetDatabaseName());

    if( Database.Login(GetDatabaseUser(),GetDatabasePassword()) == false ) {
        ES_ERROR("unable to login to the database");
        return(false);
    };

    return(true);
}

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================

bool CAMSStatServer::ExecuteServer(void)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int  s;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    s = getaddrinfo(NULL,CSmallString(GetPortNumber()), &hints, &result);
    if(s != 0) {
        CSmallString error;
        error << "getaddrinfo: " << gai_strerror(s);
        ES_ERROR(error);
        return(false);
    }

    //  getaddrinfo() returns a list of address structures.
    // Try each address until we successfully bind(2).
    // If socket(2) (or bind(2)) fails, we (close the socket
    // and) try the next address.

    for(rp = result; rp != NULL; rp = rp->ai_next) {
        Socket = socket(rp->ai_family, rp->ai_socktype,
                        rp->ai_protocol);
        if( Socket == -1 ) continue;

        if( bind(Socket, rp->ai_addr, rp->ai_addrlen) == 0) break; // Success

        close(Socket);
    }

    if( rp == NULL ) { // No address succeeded
        ES_ERROR("could not bind");
        return(false);
    }

    freeaddrinfo(result); // No longer needed

    int counted_requests = 0;
    int successful_requests = 0;

    Transaction.AssignToDatabase(&Database);

    // server loop
    while(Terminated == false) {

        CAddStatDatagram        datagram;
        struct sockaddr_storage peer_addr;
        socklen_t               peer_addr_len;
        ssize_t                 nread;

        // get datagram ------------------------------
        peer_addr_len = sizeof(struct sockaddr_storage);
        nread = recvfrom(Socket,&datagram,sizeof(datagram), 0,
                         (struct sockaddr *)&peer_addr, &peer_addr_len);

        counted_requests++;

        if(nread == -1) continue;                   // Ignore failed request
        if(nread != sizeof(datagram) ) continue;    // Ignore incomplete request

        char host[NI_MAXHOST], service[NI_MAXSERV];
        memset(host,0,NI_MAXHOST);

        // get client hostname -----------------------
        s = getnameinfo((struct sockaddr *) &peer_addr,
                        peer_addr_len, host, NI_MAXHOST-1,
                        service, NI_MAXSERV-1, NI_NUMERICSERV);
        if(s != 0) {
            // client hostname is not available
            CSmallString error;
            error << "getnameinfo: " << gai_strerror(s);
            ES_ERROR(error);
            continue;
        }

        // validate datagram -------------------------
        if( datagram.IsValid() == false ) {
            ES_ERROR("datagram is not valid (checksum error)");
            continue;
        }

        // is client authorized? ---------------------
        if( IsClientAuthorized(host) == false ) {
            CSmallString error;
            error << "client (" <<  host << ") is not authorized";
            ES_ERROR(error);
            continue;
        }

        // write data to database---------------------
        if( Transaction.StartTransaction() == false ) {
            ES_ERROR("unable to start database transaction");
            continue;
        }

        if( WriteDataToDatabase(datagram) == false ){
            ES_ERROR("unable to write datagram to database");
            Transaction.RollbackTransaction();
            continue;
        }

        if( Transaction.CommitTransaction() == false ) {
            ES_ERROR("unable to commit database transaction");
            continue;
        }

        successful_requests++;
    }

    vout << endl;
    vout << "Number of requests  : " << counted_requests << endl;
    vout << "Successful requests : " << successful_requests << endl;

    // clean-up -------------------------------------
    //close(sfd); //it is closed in ShutdownServer
    Database.Logout();

    return(true);
}

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================

bool CAMSStatServer::WriteDataToDatabase(CAddStatDatagram& datagram)
{
    CFirebirdExecuteSQL sql_exec;
    sql_exec.AssignToTransaction(&Transaction);

    if( sql_exec.AllocateInputItems(14) == false ) {
        ES_ERROR("unable to allocate items for ExecuteSQL");
        return(false);
    }

    // set items
    sql_exec.GetInputItem(0)->SetInt(GetKeyID(datagram.GetSite()));
    sql_exec.GetInputItem(1)->SetInt(GetKeyID(datagram.GetModuleName()));
    sql_exec.GetInputItem(2)->SetInt(GetKeyID(datagram.GetModuleVers()));
    sql_exec.GetInputItem(3)->SetInt(GetKeyID(datagram.GetModuleArch()));
    sql_exec.GetInputItem(4)->SetInt(GetKeyID(datagram.GetModuleMode()));
    sql_exec.GetInputItem(5)->SetInt(GetKeyID(datagram.GetUser()));
    sql_exec.GetInputItem(6)->SetInt(GetKeyID(datagram.GetHostName()));
    sql_exec.GetInputItem(7)->SetInt(datagram.GetNCPUs());
    sql_exec.GetInputItem(8)->SetInt(datagram.GetNumOfHostCPUs());
    sql_exec.GetInputItem(9)->SetInt(datagram.GetNGPUs());
    sql_exec.GetInputItem(10)->SetInt(datagram.GetNumOfHostGPUs());
    sql_exec.GetInputItem(11)->SetInt(datagram.GetNumOfNodes());
    sql_exec.GetInputItem(12)->SetInt(datagram.GetFlags());
    sql_exec.GetInputItem(13)->SetTimeAndDate(datagram.GetTimeAndDate());

    CSmallString sql;

    sql = "INSERT INTO \"STATISTICS\" (\"Site\",\"ModuleName\",\"ModuleVers\",\"ModuleArch\","
          "\"ModuleMode\",\"User\",\"HostName\",\"NCPUS\",\"NHostCPUS\",\"NGPUS\",\"NHostGPUS\",\"NNODES\","
          "\"Flags\",\"Time\") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)";

    // execute SQL statement
    if( sql_exec.ExecuteSQL(sql) == false ) {
        ES_ERROR("unable to execute SQL statement");
        return(false);
    }

    return(true);
}

//------------------------------------------------------------------------------

int CAMSStatServer::GetKeyID(const CSmallString& key)
{
    // find key id
    CFirebirdQuerySQL sql_query;
    sql_query.AssignToTransaction(&Transaction);

    CSmallString sql;

    sql = "SELECT \"ID\" FROM \"KEYS\" WHERE \"Key\" = ?";

    if( sql_query.PrepareQuery(sql) == false ){
        ES_ERROR("unable to prepare sql query");
        return(false);
    }

    sql_query.GetInputItem(0)->SetString(key);

    if( sql_query.ExecuteQueryOnce() == true ){
        // key exists - return its value
        return(sql_query.GetOutputItem(0)->GetInt());
    }

    // create new key

    CFirebirdExecuteSQL sql_exec;
    sql_exec.AssignToTransaction(&Transaction);

    if( sql_exec.AllocateInputItems(1) == false ) {
        ES_ERROR("unable to allocate items for ExecuteSQL");
        return(false);
    }

    // set items
    sql_exec.GetInputItem(0)->SetString(key);

    sql = "INSERT INTO \"KEYS\" (\"Key\") VALUES(?)";

    // execute SQL statement
    if( sql_exec.ExecuteSQL(sql) == false ) {
        ES_ERROR("unable to execute SQL statement");
        return(false);
    }

    // get key value
    sql = "SELECT \"ID\" FROM \"KEYS\" WHERE \"Key\" = ?";

    if( sql_query.PrepareQuery(sql) == false ){
        ES_ERROR("unable to prepare sql query");
        return(false);
    }

    sql_query.GetInputItem(0)->SetString(key);

    if( sql_query.ExecuteQueryOnce() == false ){
        // key exists - return its value
        CSmallString error;
        error << "unable to create key '" << key << "'";
        ES_ERROR(error);
        return(-1);
    }

    return(sql_query.GetOutputItem(0)->GetInt());
}

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================

bool CAMSStatServer::ShutdownServer(void)
{
    Terminated = true;
    close(Socket);
    return(true);
}

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================

bool CAMSStatServer::IsClientAuthorized(const char* p_name)
{
    CXMLElement* p_ele = ServerConfig.GetChildElementByPath("config/clients");
    if( p_ele == NULL ) {
        // no clients -> everything is allowed
        return(true);
    }

    CXMLIterator I(p_ele);
    CXMLElement* p_cele;

    while((p_cele = I.GetNextChildElement("client")) != NULL ) {
        CSmallString mask;
        if( p_cele->GetAttribute("name",mask) == true ) {
            if( fnmatch(mask,p_name,0) == 0 ) return(true);
        }
    }

    return(false);
}

//------------------------------------------------------------------------------

const CSmallString CAMSStatServer::GetDatabaseName(void)
{
    CSmallString setup = "local:ams_stat.fdb";
    CXMLElement* p_ele = ServerConfig.GetChildElementByPath("config");
    if( p_ele == NULL ) {
        ES_ERROR("unable to open config path");
        return(setup);
    }
    if( p_ele->GetAttribute("database",setup) == false ) {
        ES_ERROR("unable to get setup item");
        return(setup);
    }
    return(setup);
}

//------------------------------------------------------------------------------

const CSmallString CAMSStatServer::GetDatabaseUser(void)
{
    CSmallString setup = "ams";
    CXMLElement* p_ele = ServerConfig.GetChildElementByPath("config");
    if( p_ele == NULL ) {
        ES_ERROR("unable to open config path");
        return(setup);
    }
    if( p_ele->GetAttribute("user",setup) == false ) {
        ES_ERROR("unable to get setup item");
        return(setup);
    }
    return(setup);
}

//------------------------------------------------------------------------------

const CSmallString CAMSStatServer::GetDatabasePassword(void)
{
    CSmallString setup;
    CXMLElement* p_ele = ServerConfig.GetChildElementByPath("config");
    if( p_ele == NULL ) {
        ES_ERROR("unable to open config path");
        return(setup);
    }
    if( p_ele->GetAttribute("password",setup) == false ) {
        ES_ERROR("unable to get setup item");
        return(setup);
    }
    return(setup);
}

//------------------------------------------------------------------------------

int CAMSStatServer::GetPortNumber(void)
{
    int setup = 32597;
    CXMLElement* p_ele = ServerConfig.GetChildElementByPath("config");
    if( p_ele == NULL ) {
        ES_ERROR("unable to open config path");
        return(setup);
    }
    if( p_ele->GetAttribute("port",setup) == false ) {
        ES_ERROR("unable to get setup item");
        return(setup);
    }
    return(setup);
}

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================
