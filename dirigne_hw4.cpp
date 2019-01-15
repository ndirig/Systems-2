/* 
 * Noah Dirig Copyright 2018
 */

// This is a textual shell problem analogous to bash.  Runs commands directly from
// prompt.  Also allows user to run commands from a separate file (in our case,
// simple.sh) and execute them either in series or in parallel.

// BASE CASE
/* > # Lines starting with pound signs are to be ignored.↵ * > echo "hello, world!" ↵ * Running: echo hello, world! * hello, world! * Exit code: 0 * > ↵ * > head -2 /proc/cpuinfo↵ * Running: head -2 /proc/cpuinfo * processor : 0 * vendor_id : GenuineIntel * Exit code: 0 * > ↵ * > # A regular sleep test. ↵ * > sleep 1↵ * Running: sleep 1 * Exit code: 0 * > ↵ * > # Finally exit out↵ * > exit↵
 */

// SERIAL CASE
/* > echo "serial test take about 5 seconds"↵ * Running: echo serial test take about 5 seconds * serial test take about 5 seconds * Exit code: 0 * > SERIAL simple.sh↵ * Running: sleep 1 * Exit code: 0 * Running: sleep 1s * Exit code: 0 * Running: sleep 1.01 * Exit code: 0 * Running: sleep 0.99 * Exit code: 0 * Running: sleep 1s * Exit code: 0 * > exit↵
 */

// PARALLEL CASE
/* > # echo "parallel test take about 1 second"↵ * > PARALLEL simple.sh↵ * Running: sleep 1 * Running: sleep 1.01 * Running: sleep 1s * Running: sleep 0.99
 * Running: sleep 1s * Exit code: 0 * Exit code: 0 * Exit code: 0 * Exit code: 0 * Exit code: 0 * > exit↵
 */

#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <vector>

using namespace std;

/*
 * 1: Get repeated "> " prompt working
 * 2: "exit" command (Case 1)
 * 3: "SERIAL" (Case 2)
 * 4: "PARALLEL" (Case 3)
 * 5: Interpret as command (Case 4)
 */

int shellCommand(istream& in);
int forkExec(string& command);
void serial(vector<string>& commands);
void parallel(vector<string>& commands);
vector<string> gatherArguments(string& line);
void myExec(vector<string> argList);
vector<string> gatherFileCommands(stringstream& in);
void executeFromShell(string& line);

/**
 * Asks the user for input.  Assumes user will input:
 * 1. "SERIAL [filename]"
 * 2. "PARALLEL [filename]"
 * 3. "exit"
 * If none of the above, assume input is a command 
 * (such as echo "hello, world!")
 * @param in
 * @return exitCode :: 1 = exit, 0 = continue shell prompt
 */
int shellCommand(istream& in) {
    string command, line;
    getline(in, line);
    stringstream inStream(line);
    // get first word of input line
    inStream >> command;
    // exit command (Case 1)
    if (command == "exit") {
        return 1;
    } else if (command == "SERIAL") {
        // serial command (Case 2)
        // get a list of file commands and to be executed serially
        vector<string> fileCommands = gatherFileCommands(inStream);
        serial(fileCommands);
    } else if (command == "PARALLEL") {
        // parallel command (Case 3)
        // get a list of file commands and to be executed in parallel
        vector<string> fileCommands = gatherFileCommands(inStream);
        parallel(fileCommands);
    } else if (command[0] != '#' && command != "") {
        // ignore comments (starting with "#")
        // assume first word is a program and following words are args (Case 4)
        executeFromShell(line);
    }
    return 0;
}

/**
 * Execute a command inputted directly from the shell prompt (Case 4)
 * @param line
 */
void executeFromShell(string& line) {
    int pid = forkExec(line);
    int exitCode;
    waitpid(pid, &exitCode, 0);
    // display exit code
    cout << "Exit code: " << exitCode << endl;
}

/**
 * Given a line of input in the format of:
 * "SERIAL/PARALLEL filename", open the file and put all the commands in
 * the file into a vector
 * @param in
 */
vector<string> gatherFileCommands(stringstream& in) {
    string line, fileName;
    in >> fileName;
    ifstream file(fileName);
    vector<string> fileCommands;
    // store every line in the file in a vector of commands
    while (getline(file, line)) {
        fileCommands.push_back(line);
    }
    return fileCommands;
}

/**
 * Executes commands serially
 * @param commands
 */
void serial(vector<string>& commands) {
    // fork and exec all commands
    for (auto command : commands) {
        stringstream cmdStream(command);
        // check to see if line is a comment
        string firstArg;
        cmdStream >> firstArg;
        if (firstArg[0] != '#' && firstArg != "") {
            // fork and exec the command
            int pid = forkExec(command);
            int exitCode;
            waitpid(pid, &exitCode, 0);
            // display exit code
            cout << "Exit code: " << exitCode << endl;
        }
    }
}

/**
 * Executes commands in parallel
 * @param commands
 */
void parallel(vector<string>& commands) {
    /* FIRST GATHER ARGUMENTS FOR COMMAND THEN (IF '#', REJECT)
     * PRINT OUT "Running: "
     * THEN PRINT OUT ALL ARGS
     * THEN PRINT OUTPUT
     * THEN PRINT EXIT CODE (which is output of waitpid (type pid_t))
     */
    
    // store all pids in vector, once we fork and exec all commands
    // we will go and use waitpid for every pid in the pid vector
    vector<int> pids;
    for (auto command : commands) {
        stringstream cmdStream(command);
        // check to see if line is a comment
        string firstArg;
        cmdStream >> firstArg;
        if (firstArg[0] != '#' && firstArg != "") {
            // fork and exec the command, pid is place in a vector for later
            pids.push_back(forkExec(command));
        }
    }
    // waitpid for every command after they have been forked
    for (int pid : pids) {
        int exitCode;
        waitpid(pid, &exitCode, 0);
        // display exit code
        cout << "Exit code: " << exitCode << endl;
    }
}

/**
 * Forks and exec to execute a command
 * @param cmdStream
 * @return 
 */
int forkExec(string& command) {
    int pid = fork();
    if (pid == 0) {
        // get arguments for the command
        vector<string> arguments = gatherArguments(command);
        myExec(arguments);
    }
    return pid;
}

/**
 * Takes arguments and executes them
 * @param argList
 */
void myExec(vector<string> argList) {
    vector<char*> args;
    for (unsigned int i = 0; i < argList.size(); i++) {
        args.push_back(&argList[i][0]);
    }
    args.push_back(nullptr);
    execvp(args[0], &args[0]);
}

/**
 * Given a line of input (a command), take all the arguments and add them
 * to a vector of char*s (to use with execvp)
 * @param line
 */
vector<string> gatherArguments(string& line) {
    // put all the args into a vector of strings
    string argument;
    stringstream inStream(line);
    vector<string> arguments;
    // prints what command is running
    cout << "Running: ";
    while (inStream >> quoted(argument)) {
        arguments.push_back(argument);
        cout << " " << argument;
    }
    cout << endl;
    return arguments;
}

int main(int argc, char** argv) {
    int exit = 0;
    // loop until exit command
    while (exit == 0) {
        cout << "> ";
        // if shell prompt returns 1,
        // then the exit command has been given
        exit = shellCommand(cin);
    }
    return 0;
}
