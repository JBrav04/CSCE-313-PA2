#include <iostream>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <algorithm>

#include <vector>
#include <string>

#include "Tokenizer.h"

// all the basic colours for a shell prompt
#define RED     "\033[1;31m"
#define GREEN	"\033[1;32m"
#define YELLOW  "\033[1;33m"
#define BLUE	"\033[1;34m"
#define WHITE	"\033[1;37m"
#define NC      "\033[0m"

using namespace std;

//global variables
vector<int> backgroundPIDs;

//helper functions

string getCurrentDir() {
    char buffer[PATH_MAX];
    string cwd;

    if (getcwd(buffer, sizeof(buffer)) != nullptr) {
        cwd = buffer;
    }
    else {
        perror("getcwd() error");
    }
    
    return cwd;
}

string promptText() {
    
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);

    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%b %d %H:%M:%S", t);

    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));

    const char* user = getenv("USER");
    if (!user) user = "user";

    std::string prompt = std::string(timeStr) + " " + user + ":" + cwd + "$ ";
    return prompt;
}


string changeDirectory(Tokenizer& tknr, string prevwd) {
    //cout << tknr.commands.at(0)->args.at(0) << endl;
    string cwd = getCurrentDir();
    const char* newDir = (tknr.commands.at(0)->args.at(1) == "-")? 
        prevwd.c_str() : tknr.commands.at(0)->args.at(1).c_str();

    if (chdir(newDir) != 0) {
        cerr << "directory not found" << endl;
    }

    return cwd;
}

void processInput(const char* file) {
    int fd = open(file, O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(2);
    }

    if (dup2(fd, STDIN_FILENO) == -1) { 
        perror("dup2"); 
        close(fd);
        exit(2);
    }
    close(fd);
}

void processOutput(const char* file) {
    int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        exit(2);
    }

    if (dup2(fd, STDOUT_FILENO) == -1) { 
        perror("dup2"); 
        close(fd);
        exit(2);
    }
    close(fd);
}

pid_t forkProcess() {
    pid_t pid = fork();
    if (pid < 0) {  // error check
        perror("fork");
        exit(2);
    }
    return pid;
}


void processCommand(Command* cmd, bool piped = false) {
    pid_t pid = forkProcess();

    if (pid == 0) {  // if child, exec to run command
        //handle redirection
        if (cmd->hasInput()) {
            processInput(cmd->in_file.c_str());
        }
        if (cmd->hasOutput()) {
            processOutput(cmd->out_file.c_str());
        }
        if (piped) {

        }

        vector<char*> argv;
        argv.reserve(cmd->args.size() + 1);

        for (auto& s : cmd->args) {
            argv.push_back(const_cast<char*>(s.c_str()));
        }
        argv.push_back(nullptr);

        if (execvp(argv.at(0), argv.data()) < 0) {
            perror("execvp");
            exit(2);
        }
    }
    else { 
        int status = 0;

        if (!cmd->isBackground()) {
            waitpid(pid, &status, 0);
            if (status > 1) {  // exit if child didn't exec properly
                exit(status);
            } 
        }
        else {
            backgroundPIDs.push_back(pid);
        }
    }
}

void reapBackgroundPIDs() {
    pid_t pid;
    int status = 0;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {}
}

void processPipes(vector<Command*> commands) {
    //create pipes
    size_t n = commands.size();
    vector<pid_t> pids(n);
    vector<array<int, 2>> pipes(n - 1);

    for (size_t i = 0; i < n - 1; ++i) {
        if (pipe(pipes[i].data()) == -1) {
            perror("pipe");
            exit(2);
        }
    }

    for (size_t i = 0; i < n; ++i) {
        pid_t pid = forkProcess();

        if (pid == 0) {

            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            if (i < n - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            for (auto &p : pipes) {
                close(p[0]);
                close(p[1]);
            }

            if (commands.at(i)->hasInput()) {
                processInput(commands.at(i)->in_file.c_str());
            }

            if (commands.at(i)->hasOutput()) {
                processOutput(commands.at(i)->out_file.c_str());
            }

            vector<char*> argv;
            argv.reserve(commands.at(i)->args.size() + 1);

            for (auto& s : commands.at(i)->args) {
                argv.push_back(const_cast<char*>(s.c_str()));
            }
            argv.push_back(nullptr);

            if (execvp(argv.at(0), argv.data()) < 0) {
                perror("execvp");
                exit(2);
            }
        }
        pids[i] = pid;
    }

    for (auto &p : pipes) {
        close(p[0]);
        close(p[1]);
    }

    for (pid_t pid : pids) {
        waitpid(pid, nullptr, 0);
    }
}


int main () {
    //intitialize variables
    string prevwd = getCurrentDir();

    //command prompt loop
    for (;;) {
        //check bg processes
        reapBackgroundPIDs();

        cout << YELLOW << promptText() << NC << flush;
        
        // get user inputted command
        string input;
        getline(cin, input);

        if (input == "exit") {  // print exit message and break out of infinite loop
            cout << RED << "Now exiting shell..." << endl << "Goodbye" << NC << endl;
            break;
        }

        // get tokenized commands from user input
        Tokenizer tknr(input);
        if (tknr.hasError()) {  // continue to next prompt if input had an error
            cout << "Invalid Input" << endl;
            continue;
        }
        
        //handle commands
        if (tknr.commands.at(0)->args.at(0) == "cd") {
            prevwd = changeDirectory(tknr, prevwd);
        }
        else if (tknr.commands.size() == 1) {
            processCommand(tknr.commands.at(0));
        }
        else {
            processPipes(tknr.commands);
        }

    }
}
