/*
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS HEADER.
 *
 * Copyright 2009-2010 JRuby Team (www.jruby.org).
 * Copyright 1997-2008 Sun Microsystems, Inc. All rights reserved.
 *
 * The contents of this file are subject to the terms of either the GNU
 * General Public License Version 2 only ("GPL") or the Common
 * Development and Distribution License("CDDL") (collectively, the
 * "License"). You may not use this file except in compliance with the
 * License. You can obtain a copy of the License at
 * http://www.netbeans.org/cddl-gplv2.html
 * or nbbuild/licenses/CDDL-GPL-2-CP. See the License for the
 * specific language governing permissions and limitations under the
 * License.  When distributing the software, include this License Header
 * Notice in each file and include the License file at
 * nbbuild/licenses/CDDL-GPL-2-CP.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the GPL Version 2 section of the License file that
 * accompanied this code. If applicable, add the following below the
 * License Header, with the fields enclosed by brackets [] replaced by
 * your own identifying information:
 * "Portions Copyrighted [year] [name of copyright owner]"
 *
 * Contributor(s):
 *
 * The Original Software is NetBeans. The Initial Developer of the Original
 * Software is Sun Microsystems, Inc. Portions Copyright 1997-2008 Sun
 * Microsystems, Inc. All Rights Reserved.
 *
 * If you wish your version of this file to be governed by only the CDDL
 * or only the GPL Version 2, indicate your decision by adding
 * "[Contributor] elects to include this software in this distribution
 * under the [CDDL or GPL Version 2] license." If you do not indicate a
 * single choice of license, a recipient has the option to distribute
 * your version of this file under either the CDDL, the GPL Version 2 or
 * to extend the choice of license to its licensees as provided above.
 * However, if you add GPL Version 2 code and therefore, elected the GPL
 * Version 2 license, then the option applies only if the new code is
 * made subject to such option by the copyright holder.
 *
 * Author: Tomas Holy
 */

#include "utilsfuncs.h"
#include "platformlauncher.h"

using namespace std;

extern "C" int nailgunClientMain(int argc, char *argv[], char *env[]);

PlatformLauncher::PlatformLauncher()
    : ArgParser()
    , suppressConsole(false)
{
}

PlatformLauncher::PlatformLauncher(const PlatformLauncher& orig)
    : ArgParser(orig)
{
}

PlatformLauncher::~PlatformLauncher() {
}

list<string>* GetEnvStringsAsList() {
    char * env = GetEnvironmentStrings();
    list<string> * envList = new list<string>();
    while (*env) {
        envList->push_back(env);
        env = env + strlen(env) + 1;
    }
    return envList;
}

const char** convertToArgvArray(list<string> args) {
    const char ** argv = (const char**) malloc(sizeof (char*) * args.size());
    int i = 0;
    for (list<string>::iterator it = args.begin(); it != args.end(); ++it, ++i) {
        argv[i] = it->c_str();
    }
    return argv;
}

bool PlatformLauncher::start(char* argv[], int argc, DWORD *retCode, const char* binaryName) {
    if (!checkLoggingArg(argc, argv, false)
	|| !initPlatformDir()
	|| !parseArgs(argc, argv)
	|| !checkJDKHome()) {
        return false;
    }
    disableFolderVirtualization(GetCurrentProcess());

    if (nailgunClient) {
        progArgs.push_front("org.jruby.util.NailMain");
        const char ** nailArgv = convertToArgvArray(progArgs);
        list<string>* envList = GetEnvStringsAsList();
        const char ** nailEnv  = convertToArgvArray(*envList);
        nailgunClientMain(progArgs.size(), (char**)nailArgv, (char**)nailEnv);
        return true;
    }

    if (binaryName) {
        // clean up the binaryName first,
        // remove '.exe' from the end, and the possible path.
        string bn = binaryName;

        size_t found = bn.find_last_of("/\\");
        if (found != string::npos) {
            logMsg("The binary name contains slashes, will remove: %s", binaryName);
            bn = bn.substr(found + 1);
            binaryName = bn.c_str();
        }

        found = bn.find(".exe", bn.length() - 4);
        if (found != string::npos) {
            bn.erase(found, 4);
            binaryName = bn.c_str();
            logMsg("*** Cleaned up the binary name: %s", binaryName);
        } else {
            logMsg("*** No need to clean the binary name: %s", binaryName);
        }

        if (strnicmp(binaryName, DEFAULT_EXECUTABLE, strlen(DEFAULT_EXECUTABLE)) != 0) {
            logMsg("PlatformLauncher:\n\tNon-default executable name: %s", binaryName);
            logMsg("\tHence, launching with extra parameters: -S %s", binaryName);
            progArgs.push_front(binaryName);
            progArgs.push_front("-S");
        }
    }

    if (jdkhome.empty()) {
        if (!jvmLauncher.initialize(REQ_JAVA_VERSION)) {
            logErr(false, true, "Cannot find Java %s or higher.", REQ_JAVA_VERSION);
            return false;
        }
    }
    jvmLauncher.getJavaPath(jdkhome);

    prepareOptions();

    if (nextAction.empty()) {
        while (true) {
            // run app
            if (!run(retCode)) {
                return false;
            }

            break;
        }
    } else {
        logErr(false, true, "We should not get here.");
        return false;
    }

    return true;
}

bool PlatformLauncher::run(DWORD *retCode) {
    logMsg("Starting application...");
    constructBootClassPath();
    constructClassPath();
    const char *mainClass;
    mainClass = bootclass.empty() ? MAIN_CLASS : bootclass.c_str();

    // replace '/' by '.' to report a better name to jps/jconsole
    string cmdName = mainClass;
    size_t position = cmdName.find("/");
    while (position != string::npos) {
      cmdName.replace(position, 1, ".");
      position = cmdName.find("/", position + 1);
    }

    string option = OPT_JRUBY_COMMAND_NAME;
    option += cmdName;
    javaOptions.push_back(option);

    option = OPT_CLASS_PATH;
    option += classPath;
    javaOptions.push_back(option);

    if (!bootClassPath.empty()) {
        option = OPT_BOOT_CLASS_PATH;
        option += bootClassPath;
        javaOptions.push_back(option);
    }

    jvmLauncher.setSuppressConsole(suppressConsole);
    bool rc = jvmLauncher.start(mainClass, progArgs, javaOptions, separateProcess, retCode);
    if (!separateProcess) {
        exit(0);
    }

    javaOptions.pop_back();
    javaOptions.pop_back();
    return rc;
}

bool PlatformLauncher::initPlatformDir() {
    char path[MAX_PATH] = "";
    getCurrentModulePath(path, MAX_PATH);
    logMsg("Module: %s", path);
    char *bslash = strrchr(path, '\\');
    if (!bslash) {
        return false;
    }
    *bslash = '\0';
    bslash = strrchr(path, '\\');
    if (!bslash) {
        return false;
    }
    *bslash = '\0';
    platformDir = path;
    logMsg("Platform dir: %s", platformDir.c_str());
    logMsg("classPath: %s", classPath.c_str());
    return true;
}

bool PlatformLauncher::checkJDKHome() {
    if (!jdkhome.empty() && !jvmLauncher.initialize(jdkhome.c_str())) {
	logMsg("Cannot locate java installation in specified jdkhome: %s", jdkhome.c_str());
	string errMsg = "Cannot locate java installation in specified jdkhome:\n";
	errMsg += jdkhome;
	errMsg += "\nDo you want to try to use default version?";
	jdkhome = "";
	if (::MessageBox(NULL, errMsg.c_str(), "Invalid jdkhome specified", MB_ICONQUESTION | MB_YESNO) == IDNO) {
	    return false;
	}
    }

    if (jdkhome.empty()) {
        logMsg("-Xjdkhome is not set, checking for %%JAVA_HOME%%...");
        char *javaHome = getenv("JAVA_HOME");
        if (javaHome) {
            logMsg("%%JAVA_HOME%% is set: %s", javaHome);
            if (!jvmLauncher.initialize(javaHome)) {
                logMsg("Cannot locate java installation, specified by JAVA_HOME: %s", javaHome);
                string errMsg = "Cannot locate java installation, specified by JAVA_HOME:\n";
                errMsg += javaHome;
                errMsg += "\nDo you want to try to use default version?";
                jdkhome = "";
                if (::MessageBox(NULL, errMsg.c_str(),
                        "Invalid jdkhome specified", MB_ICONQUESTION | MB_YESNO) == IDNO) {
                    return false;
                }
            } else {
                jdkhome = javaHome;
            }
        }
    }

    return true;
}

void PlatformLauncher::onExit() {
    logMsg("onExit()");
}
