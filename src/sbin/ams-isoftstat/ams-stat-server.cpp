// ===============================================================================
// AMS - Advanced Module System
// -------------------------------------------------------------------------------
//    Copyright (C) 2004,2005,2008 Petr Kulhanek, kulhanek@chemi.muni.cz
//
//     This program is free software; you can redistribute it and/or modify
//     it under the terms of the GNU General Public License as published by
//     the Free Software Foundation; either version 2 of the License, or
//     (at your option) any later version.
//
//     This program is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU General Public License for more details.
//
//     You should have received a copy of the GNU General Public License along
//     with this program; if not, write to the Free Software Foundation, Inc.,
//     51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
// ===============================================================================

#include "ams-stat-server-opts.h"
#include <ErrorSystem.hpp>
#include <Statistics.hpp>
#include <signal.h>
#include "AMSStatServer.hpp"
#include <unistd.h>

//------------------------------------------------------------------------------

CModuleAddStatOpts  Options;
CAMSStatServer      Server;
bool                Child = false;

//------------------------------------------------------------------------------

int Init(int argc, char* argv[]);
bool Run(void);
bool Finalize(void);
void CtrlCSignalHandler(int signal);

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================

int main(int argc, char* argv[])
{
    int result = Init(argc,argv);

    switch(result) {
    case SO_EXIT:
        return(0);
    case SO_CONTINUE: {
        bool bresult = Run();
        if( bresult == false ) {
            if( Options.GetOptVerbose() == false ) fprintf(stderr,"\n");
            ErrorSystem.PrintLastError("ams-stat-server");
            if( Options.GetOptVerbose() == false ) fprintf(stderr,"\n");
        }
        if( Child == true ) {
            Finalize();
        } else {
            if( Options.GetOptDaemon() == false ) {
                Finalize();
            } else {
                if( bresult == false ) Finalize();
            }
        }
        if( bresult ) {
            return(0);
        } else {
            return(1);
        }
    }

    case SO_USER_ERROR:
        if( Options.GetOptVerbose() == false ) fprintf(stderr,"\n");
        ErrorSystem.PrintLastError("ams-stat-server");
        if( Options.GetOptVerbose() == false ) fprintf(stderr,"\n");
        Finalize();
        return(2);

    case SO_OPTS_ERROR:
    default:
        return(3);
    }
}

//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================



//==============================================================================
//------------------------------------------------------------------------------
//==============================================================================



