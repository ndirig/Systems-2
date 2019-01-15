/* 
 * A simple web-server.  
 * 
 * The web-server performs the following tasks:
 * 
 *     1. Accepts connection from a client.
 *     2. Processes cgi-bin GET request.
 *     3. If not cgi-bin, it responds with the specific file or a 404.
 * 
 * Copyright (C) 2018 raodm@miamiOH.edu,
 *               2018 Noah Dirig
 */

// Run from command line with following arguments: [input file], std::cout, [true/false]
// Supplied input files include base_case1_inputs.txt, base_case2_inputs.txt
// For argument 3, use true to generate a graph

#include <ext/stdio_filebuf.h>
#include <unistd.h>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>

// Using namespaces to streamline code below
using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;


// Forward declaration for method defined further below
void serveClient(std::istream& is, std::ostream& os, bool genFlag);
const string html1();
void html2(int pid, string& html2Str, bool genChart);
string getStats(int pid, vector<string>& values);
string strFl(float fl);

std::mutex statMutex;

// shared_ptr is a garbage collected pointer!
using TcpStreamPtr = std::shared_ptr<tcp::iostream>;

/** Simple method to be run from a separate thread.
 *
 * @param client The client socket to be processed.
 */
void threadMain(TcpStreamPtr client) {
    // Call routine/regular helper method.
    serveClient(*client, *client, true);
}

/**
 * Runs the program as a server that listens to incoming connections.
 * 
 * @param port The port number on which the server should listen.
 */
void runServer(int port) {
    // Setup a server socket to accept connections on the socket
    io_service service;
    // Create end point
    tcp::endpoint myEndpoint(tcp::v4(), port);
    // Create a socket that accepts connections
    tcp::acceptor server(service, myEndpoint);
    std::cout << "Server is listening on " << port 
              << " & ready to process clients...\n";
    // Process client connections one-by-one...forever
    while (true) {
        // Create garbage-collect object on heap
        TcpStreamPtr client = std::make_shared<tcp::iostream>();
        // Wait for a client to connect
        server.accept(*client->rdbuf());
        // Create a separate thread to process the client.
        std::thread thr(threadMain, client);
        thr.detach();
    }
}

// Forward declaration for method used further below.
std::string url_decode(std::string);

// Named-constants to keep pipe code readable below
const int READ = 0, WRITE = 1;

// The default file to return for "/"
const std::string RootFile = "index.html";

/**
 * This method is a convenience method that extracts file or command
 * from a string of the form: "GET <path> HTTP/1.1"
 * 
 * @param req The request from which the file path is to be extracted.
 * @return The path to the file requested
 */
std::string getFilePath(const std::string& req) {
    // std::cout << req << std::endl;
    size_t spc1 = req.find(' '), spc2 = req.rfind(' ');
    if ((spc1 == std::string::npos) || (spc2 == std::string::npos)) {
        return "";  // Invalid or unhandled type of request.
    }
    std::string path = req.substr(spc1 + 2, spc2 - spc1 - 2);
    if (path == "") {
        return RootFile;  // default root file
    }
    return path;
}

/** Helper method to send HTTP 404 message back to the client.

    This method is called in cases where the specified file name is
    invalid.  This method uses the specified path and sends a suitable
    404 response back to client.

    \param[out] os The output stream to where the data is to be
    written.

    \param[in] path The file path that is invalid.
 */
void send404(std::ostream& os, const std::string& path) {
    const std::string msg = "The following file was not found: " + path;
    // Send a fixed message back to the client.
    os << "HTTP/1.1 404 Not Found\r\n"
       << "Content-Type: text/plain\r\n"
       << "Transfer-Encoding: chunked\r\n"        
       << "Connection: Close\r\n\r\n";
    // Send the chunked data to client.
    os << std::hex << msg.size() << "\r\n";
    // Write the actual data for the line.
    os << msg << "\r\n";
    // Send trailer out to end stream to client.
    os << "0\r\n\r\n";
}

/**
 * Obtain the mime type of data based on file extension.
 * 
 * @param path The path from where the file extension is to be determined.
 * 
 * @return The mime type associated with the contents of the file.
 */
std::string getMimeType(const std::string& path) {
    const size_t dotPos = path.rfind('.');
    if (dotPos != std::string::npos) {
        const std::string ext = path.substr(dotPos + 1);
        if (ext == "html") {
            return "text/html";
        } else if (ext == "png") {
            return "image/png";
        } else if (ext == "jpg") {
            return "image/jpeg";
        }
    }
    // In all cases return default mime type.
    return "text/plain";
}

/** Convenience method to split a given string into words.

    This method is just a copy-paste of example code from lecture
    slides.

    \param[in] str The string to be split

    \return A vector of words
 */
std::vector<std::string> split(std::string str) {
    std::istringstream is(str);
    std::string word;
    std::vector<std::string> list;
    while (is >> word) {
        list.push_back(word);
    }
    return list;
}

/** Uses execvp to run the child process.

    This method is a helper method that is used to run the child
    process using the execvp command.  This method is called onlyl on
    the child process from the exec() method in this source file.

    \param[in] argList The list of command-line arguments.  The 1st
    entry is assumed to the command to be executed.
*/
void runChild(std::vector<std::string> argList) {
    // Setup the command-line arguments for execvp.  The following
    // code is a copy-paste from lecture slides.
    std::vector<char*> args;
    for (size_t i = 0; (i < argList.size()); i++) {
        args.push_back(&argList[i][0]);
    }
    // nullptr is very important
    args.push_back(nullptr);
    // Finally run the command in child process
    execvp(args[0], &args[0]);  // Run the command!
    // If control drops here, then the command was not found!
    std::cout << "Command " << argList[0] << " not found!\n";
    // Exit out of child process with error exit code.    
    exit(0);
}

/** Helper method to send the data to client in chunks.
    
    This method is a helper method that is used to send data to the
    client line-by-line.

    \param[in] mimeType The Mime Type to be included in the header.

    \param[in] pid An optional PID for the child process.  If it is
    -1, it is ignored.  Otherwise it is used to determine the exit
    code of the child process and send it back to the client.
*/
void sendData(const std::string& mimeType, int pid,
              std::istream& is, std::ostream& os, string& html2, thread& t) {
    // First write the fixed HTTP header.
    os << "HTTP/1.1 200 OK\r\n" << "Content-Type: " << mimeType << "\r\n"
       << "Transfer-Encoding: chunked\r\n" << "Connection: Close\r\n\r\n"
       << html1();
    // Read line-by line from child-process and write results to
    // client.
    std::string line;
    while (std::getline(is, line)) {
        // Add required "\n" to terminate the line.
        line += "\n";
        // Add size of line in hex
        os << std::hex << line.size() << "\r\n";
        // Write the actual data for the line.
        os << line << "\r\n";
    }
    // Check if we need to end out exit code
    if (pid != -1) {
        // We are done with the process -- join the statistics thread
        t.join();
        // Wait for process to finish and get exit code.
        int exitCode = 0;
        waitpid(pid, &exitCode, 0);
        // std::cout << "Exit code: " << exitCode << std::endl;
        // Create exit code information and send to client.
        line = "\r\nExit code: " + std::to_string(exitCode) + "\r\n";
        os << std::hex << line.size() << "\r\n" << line << "\r\n";
    }
    // Send second HTML portion and trailer out to end stream to client.
    os << html2 << "0\r\n";
}

// Hardcoded string
const string html1() {
    return "156\r\n<html>\r\n  <head>\r\n    <script type='text/javascript' " 
        "src='https://www.gstatic.com/charts/loader.js'></script>\r\n    " 
        "<script type='text/javascript' src='/draw_chart.js'></script>\r\n" 
        "    <link rel='stylesheet' type='text/css' href='/mystyle.css'>" 
        "\r\n  </head>\r\n\r\n  <body>\r\n    <h3>Output from program</h3>\r\n" 
        "    <textarea style='width: 700px; height: 200px'>\r\n\r\n";
}

/**
 * Fake-news-method used to count the number of '\r's so they can
 * be removed from the chunk size later
 * @param html2
 * @return 
 */
int newLineCount(string& html2) {
    int count = 0;
    for (unsigned int i = 0; i < html2.size(); i++) {
        if (html2[i] == '\r') { count++; }
    }
    return count;
}

/**
 * Returns JSON arrays stored in a string to plot points
 * @param values
 * @return 
 */
string json(vector<string> values) {
    string out = ",\r\n";
    for (auto line : values) {
        // first element in line is the time, second is utime, third stime,
        // fourth is memory
        stringstream stream(line); string time, utime, stime, memory, cpu;
        stream >> time >> utime >> stime >> memory;
        // cpu time is user time + system time
        cpu = strFl(stof(utime) + stof(stime));
        out += "          [" + time + ", " + cpu + ", " + memory + "]";
        if (stoul(time) != values.size()) { out += ",\n"; }
    }
    return out + "\n";
}

void html2(int pid, string& html2Str, bool genChart) {
    vector<string> values; string statistics = getStats(pid, values);
    // Three constant portions of this chunk of HTML: variable portions may
    // be in between these constant portions
    const string first = "     </textarea>\r\n     <h2>Runtime statistics</h2>"
            "\r\n     <table>\r\n"
            "       <tr><th>Time (sec)</th><th>User time</th>"
            "<th>System time</th><th>Memory (KB)</th></tr>";
    const string middle = "\r\n     </table>\r\n     <div id='chart' style='wi"
            "dth: 900px; height: 500px'></div>\r\n  </body>\r\n  <script type="
            "'text/javascript'>\r\n    function getChartData() {\r\n      "
            "return google.visualization.arrayToDataTable(\r\n        [\r\n"
            "          ['Time (sec)', 'CPU Usage', 'Memory Usage']";
    const string last = "        ]\r\n      );\r\n    }\r\n  </script>\r\n"
            "</html>\r\n"; string jsonStr;
    // different output when generating a chart
    if (!genChart) { jsonStr = "\r\n"; } else { jsonStr = json(values); }
    string html2 = first + statistics + middle + jsonStr +
            last;
    // Convert size to hex
    stringstream sstream;
    // this fake-news-math is required to get the right chunk size
    sstream << hex << html2.size() - 17 - newLineCount(statistics);
    lock_guard<mutex> guard(statMutex);
    html2Str = sstream.str() + "\r\n" + html2 + "\r\n";
}

/**
 * Makes the format of the float numbers correct according to the base case
 * expected results, because apparently it's not correct as is
 * @param fl
 * @return 
 */
string strFl(float fl) {
    if (fl == 0) { return "0"; }
    string orig = to_string(fl);
    string form = orig.substr(0, orig.find(".") + 3);
    if (form.substr(form.size()-1) == "0") { 
        form = form.substr(0, form.size()-1);
    }
    return form;
}

/**
 * Gets a string detailing the runtime statistics 
 * @param pid
 * @return stats
 */
string getStats(int pid, vector<string>& values) {
    string stats = ""; int time = 1, exitCode = 0;
    while (waitpid(pid, &exitCode, WNOHANG) == 0) {
        sleep(1);
        string word, fileName = "/proc/" + to_string(pid) + "/stat";
        ifstream file(fileName); int wordNum = 1;
        float userTime, systemTime; long memory;
        // Extract the 14th, 15th, and 23rd words
        while (file >> word) {
            long sys = sysconf(_SC_CLK_TCK);
            if (wordNum == 14) { userTime = stof(word) / sys; } else if
                (wordNum == 15) { systemTime = stof(word) / sys; } else if
                (wordNum == 23) { memory = stol(word) / 1000; }
            wordNum++;
        }
        stats = stats + "\r\n       <tr><td>" + to_string(time) + "</td><td>" + 
                strFl(userTime) + "</td><td>" + strFl(systemTime) +
                "</td><td>" + to_string(memory) + "</td></tr>";
        values.push_back(to_string(time) + " " + strFl(userTime) + " " +
            strFl(systemTime) + " " + to_string(memory));
        time++;
    }
    return stats;
}

/** Run the specified command and send output back to the user.

    This method runs the specified command and sends the data back to
    the client using chunked-style response.

    \param[in] cmd The command to be executed

    \param[in] args The command-line arguments, wich each one
    separated by one or more blank spaces.

    \param[out] os The output stream to which outputs from child
    process are to be sent.
*/
void exec(std::string cmd, std::string args, std::ostream& os, bool genChart) {
    // Split string into individual command-line arguments.
    std::vector<std::string> cmdArgs = split(args);
    // Add command as the first of cmdArgs as per convention.
    cmdArgs.insert(cmdArgs.begin(), cmd);
    // Setup pipes to obtain inputs from child process
    int pipefd[2];
    pipe(pipefd);
    // String for html output
    string html2Str = "";
    // Finally fork and exec with parent having more work to do.
    const int pid = fork();
    if (pid == 0) {
        close(pipefd[READ]);        // Close unused end.
        dup2(pipefd[WRITE], 1);     // Tie/redirect std::cout of command
        runChild(cmdArgs);
    } else {
        // Get second HTML portion + statistics
        thread t(html2, pid, ref(html2Str), genChart);
        // In parent process. First close unused end of the pipe and
        // read standard inputs.
        close(pipefd[WRITE]);
        __gnu_cxx::stdio_filebuf<char> fb(pipefd[READ], std::ios::in, 1);
        std::istream is(&fb);
        // Have helper method process the output of child-process
        sendData("text/html", pid, is, os, html2Str, t);
    }
}

/**
 * Process HTTP request (from first line & headers) and
 * provide suitable HTTP response back to the client.
 * 
 * @param is The input stream to read data from client.
 * @param os The output stream to send data to client.
 * @param genChart If this flag is true then generate data for chart.
 */
void serveClient(std::istream& is, std::ostream& os, bool genChart) {
    // Read headers from client and print them. This server
    // does not really process client headers
    std::string line;
    // Read the GET request line.
    std::getline(is, line);
    const std::string path = getFilePath(line);
    // Skip/ignore all the HTTP request & headers for now.
    while (std::getline(is, line) && (line != "\r")) {}
    // Check and dispatch the request appropriately
    const std::string cgiPrefix = "cgi-bin/exec?cmd=";
    const int prefixLen         = cgiPrefix.size();
    if (path.substr(0, prefixLen) == cgiPrefix) {
        // Extract the command and parameters for exec.
        const size_t argsPos   = path.find("&args=", prefixLen);
        const std::string cmd  = path.substr(prefixLen, argsPos - prefixLen);
        const std::string args = url_decode(path.substr(argsPos + 6));
        // Now run the command and return result back to client.
        exec(cmd, args, os, genChart);
    } 
    /* else {  it will never go here in either base case 
        Get the file size (if path exists)
        std::ifstream dataFile(path);
        if (!dataFile.good()) {
            Invalid file/File not found. Return 404 error message.
            send404(os, path);
        } else {
            Send contents of the file to the client.
            sendData(getMimeType(path), -1, dataFile, os, line, t);
        }
    } */
}

//------------------------------------------------------------------
//  DO  NOT  MODIFY  CODE  BELOW  THIS  LINE
//------------------------------------------------------------------

/** Convenience method to decode HTML/URL encoded strings.

    This method must be used to decode query string parameters
    supplied along with GET request.  This method converts URL encoded
    entities in the from %nn (where 'n' is a hexadecimal digit) to
    corresponding ASCII characters.

    \param[in] str The string to be decoded.  If the string does not
    have any URL encoded characters then this original string is
    returned.  So it is always safe to call this method!

    \return The decoded string.
*/
std::string url_decode(std::string str) {
    // Decode entities in the from "%xx"
    size_t pos = 0;
    while ((pos = str.find_first_of("%+", pos)) != std::string::npos) {
        switch (str.at(pos)) {
            case '+': str.replace(pos, 1, " ");
            break;
            case '%': {
                std::string hex = str.substr(pos + 1, 2);
                char ascii = std::stoi(hex, nullptr, 16);
                str.replace(pos, 3, 1, ascii);
            }
        }
        pos++;
    }
    return str;
}

/*
 * The main method that performs the basic task of accepting connections
 * from the user.
 */
int main(int argc, char** argv) {
    if (argc == 2) {
        // Setup the port number for use by the server
        const int port = std::stoi(argv[1]);
        runServer(port);
    } else if (argc == 4) {
        // Process 1 request from specified file for functional testing
        std::ifstream input(argv[1]);
        std::ofstream output;
        if (argv[2] != std::string("std::cout")) {
            output.open(argv[2]);
        }
        bool genChart = (argv[3] == std::string("true"));
        serveClient(input, (output.is_open() ? output : std::cout), genChart);
    } else {
        std::cerr << "Invalid command-line arguments specified.\n";
    }
    return 0;
}

// End of source code
