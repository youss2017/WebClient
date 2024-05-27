#include "Socket.hpp"
#include "CppUtility.hpp"
#include <list>
#include <thread>
#include <filesystem>
using namespace std;

struct file_transfer_header {
    uint16_t opcode;
    uint16_t size;
};

class file_transfer_host {

public:
    file_transfer_host() {
        sw::Startup();
        m_socket = sw::Socket(sw::SocketType::TCP);
        m_client = sw::Socket(sw::SocketType::TCP);
        m_socket.Bind(sw::SocketInterface::Any).Listen(1000).SetBlockingMode(false);
    }

    void process() {
        accept_clients();
        process_requests();
    }

    void start_terminal_interface() {
        thread t1([&] { this->terminal_interface(); });
        t1.detach();
    }

private:
    enum class Action {
        error,
        connect_to,
        send_file,
        send_dir
    };

    enum class ParserState {
        FetchingAction,
        FetchingAddress,
        FetchingFileName,
        FetchingDirName,
        FetchingNull
    };

    enum ParserError {
        NoError,
        ConnectToDNSFailed,
        UnknownArgument,
        ErrorParsingPort
    };

    struct ParseResult {
        Action action = Action::error;
        ParserState state = ParserState::FetchingAction;
        ParserError error = ParserError::NoError;
        string connect_to_addr;
        uint16_t connect_port = 80;
        string fileName, dirName;
    };

    enum class TransferOpCode : uint16_t {
        Error,
        WriteLastFile,
        CreateDirectory,
        CreateFile,
        SelectFile
    };

    struct TransferHeader {
        TransferOpCode opcode = TransferOpCode::Error;
        uint64_t length = 0;

        [[nodiscard]] string to_json() const {
            stringstream json;
            json << "{\"opcode\":" << to_string(uint16_t(opcode)) << ",\"length\":" << to_string(length) << "}";
            return json.str();
        }

        static TransferHeader from_json(const string& json) {

        }

    };

    void accept_clients() {
        if(auto client = m_socket.Accept();
                client.IsValid()) {
            m_clients.emplace_back(client);
        }
    }

    void process_requests() {

    }

    [[noreturn]] void terminal_interface() {
        sw::Startup();

        LOG(INFO, "File Transfer Terminal Interface Start.");

        read_user_input:
        auto szBuffer = read_terminal_input();

        /*
         * Commands:
         * connect othercomputer.com [or ip address]
         * send_file ./myfile.txt
         * send_dir ./mydir
         */
        ParseResult result = parse_user_input(szBuffer);
        const Action& action = result.action;
        const ParserError& error = result.error;
        const string& connect_to_addr = result.connect_to_addr;
        const uint16_t& connect_port = result.connect_port;

        const string &fileName = result.fileName, &dirName = result.dirName;

        switch(error) {
            case ParserError::ConnectToDNSFailed:
                LOG(ERR, "DNS look up failed.");
                break;
            case ParserError::UnknownArgument:
                LOG(ERR, "Unknown Arguments were entered.");
                break;
            case ParserError::ErrorParsingPort:
                LOG(ERR, "Failed to parse the port.");
                break;
            default: break;
        }

        if(error != ParserError::NoError || action == Action::error)
            goto read_user_input;

        if(action == Action::connect_to) goto connect_to;
        if(action == Action::send_file) goto send_file;
        if(action == Action::send_dir) goto send_dir;

        // fallback
        LOG(ERR, "Action Unhandled.");
        goto read_user_input;

        connect_to:
        {
            connect_to(connect_to_addr, connect_port);
            goto read_user_input;
        }

        send_file:
        {
            send_file(fileName);
            goto read_user_input;
        }

        send_dir:
        {
            send_dir(dirName);
            goto read_user_input;
        }
    }

    void send_file(const string& fileName) {
        if (!m_client.IsConnected()) {
            LOG(ERR, "Cannot send file if not connected.");
            return;
        }
        FILE *handle = fopen(fileName.c_str(), "rb");
        if (!handle) {
            LOG(ERR, "Cannot open file to send.");
            return;
        }

        TransferHeader header = { TransferOpCode::CreateFile, 10 };

        size_t totalSentBytes = 0;
        size_t fileSize = cpp::GetFileSize(fileName);
        auto timestamp = chrono::steady_clock::now();
        int spinnerCounter = 0;
        char spinner[] = { '\\', '|', '/', '-', ' ' };
        double downloadRate = 0.0;
        double spinnerDuration = 0.0;
        while (!feof(handle)) {
            char szFileBuf[2048];
            auto readFileBytes = (int32_t) fread(szFileBuf, sizeof(char), sizeof(szFileBuf), handle);
            printf("\r"); // clear line
            // fileSize = (readFileBytes == 0) ? totalSentBytes : fileSize;
            auto now = chrono::steady_clock::now();
            auto duration = (double)chrono::duration_cast<chrono::microseconds>(now - timestamp).count();
            downloadRate = (readFileBytes == 0) ? downloadRate : sizeof(szFileBuf) / (duration * 1e-6);
            timestamp = now;
            cout << "Sending '" << fileName << "'..." << cpp::FriendlyMemorySize((double)totalSentBytes) << "\t" << fixed << setprecision(2) << (100.0 * (double)totalSentBytes / (double)fileSize) << "%";
            cout << "\t" << spinner[(readFileBytes == 0) ? 4 : spinnerCounter] << " " << cpp::FriendlyMemorySize(downloadRate) << "/sec";
            for(int i = 0; i < 20; i++)
                putchar(' ');
            if(spinnerDuration += duration; spinnerDuration > 200e3) {
                spinnerCounter++;
                spinnerCounter %= 4;
                spinnerDuration = 0;
            }
            if(readFileBytes == 0) {
                break;
            }
            if(m_client.Send(szFileBuf, (int32_t) readFileBytes) <= 0) {
                LOG(ERR, "Error while sending file.");
                if(!m_client.IsConnected()) {
                    LOG(ERR, "Lost Connection.");
                }
            }
            totalSentBytes += readFileBytes;
        }
        cout << "\n";
        fclose(handle);
    }

    void send_dir(const string& dirName) {
        if (!m_client.IsConnected()) {
            LOG(ERR, "Cannot send directory if not connected.");
        }
        using recursive_directory_iterator = std::filesystem::recursive_directory_iterator;
        for (const auto& dirEntry : recursive_directory_iterator(dirName)) {
            if(dirEntry.is_regular_file()) {
                send_file(dirEntry.path().string());
            } else if(dirEntry.is_directory()) {

            }
        }
    }

    static vector<char> read_terminal_input() {
        printf("# ");
        vector<char> szBuffer(2048, '\n');
        if(fgets(szBuffer.data(), (int)szBuffer.size(), stdin) == nullptr) {
            // (TODO): Error encountered while reading stdin.
        }
        // truncate to actual size
        size_t readBytes = std::find(szBuffer.begin(), szBuffer.end(), '\n') - szBuffer.begin();
        szBuffer.resize(readBytes);
        return std::move(szBuffer);
    }

    static string_view read_word(const vector<char>& szBuffer, size_t& offset) {
        auto is_valid_character = [](char c) {
            return (c > ' ' && c <= '~');
        };

        bool found_first_letter = false;
        size_t start = offset;
        if(start == szBuffer.size())
            return {};
        for(size_t i = offset; i < szBuffer.size(); i++) {
            if(!found_first_letter) {
                // is this a character
                if(is_valid_character(szBuffer[i]))  {
                    found_first_letter = true;
                    start = i;
                }
            } else {
                if(!is_valid_character(szBuffer[i])) {
                    offset = i + 1;
                    return { &szBuffer[start], i - start };
                }
            }
        }
        offset = szBuffer.size();
        return { &szBuffer[start], szBuffer.size() - start };
    };

    static ParseResult parse_user_input(const vector<char>& szBuffer) {
        ParseResult result;

        string &fileName = result.fileName, &dirName = result.dirName;

        size_t offset = 0;
        // parse user input
        do {
            auto word = string(read_word(szBuffer, offset));
            if(word.empty())
                break;
            if(result.state == ParserState::FetchingAction) {
                word = cpp::LowerCase(word);
                if (word == "connect_to") {
                    result.state = ParserState::FetchingAddress;
                    result.action = Action::connect_to;
                } else if(word == "send_file") {
                    result.action = Action::send_file;
                    result.state = ParserState::FetchingFileName;
                } else if(word == "send_dir") {
                    result.action = Action::send_dir;
                    result.state = ParserState::FetchingDirName;
                }
            } else if(result.state == ParserState::FetchingAddress) {
                auto colon = word.find(':');
                string remote_device;
                if(colon != string::npos) {
                    remote_device = word.substr(0, colon);
                    auto port_string = word.substr(colon + 1);
                    size_t port64 = 80;
                    if(!cpp::TryToInt64(port_string, port64) ||
                       port64 > UINT16_MAX) {
                        result.error = ParserError::ErrorParsingPort;
                        break;
                    }
                    result.connect_port = (uint16_t)port64;
                } else
                    remote_device = word;
                if (sw::IsValidIPv4(remote_device)) {
                    result.connect_to_addr = remote_device;
                } else {
                    auto ip_addr = sw::Endpoint::GetDomainAddress(remote_device);
                    if(ip_addr.empty()) {
                        result.error = ParserError::ConnectToDNSFailed;
                        break;
                    }
                    result.connect_to_addr = ip_addr;
                }
                result.state = ParserState::FetchingNull;
            } else if(result.state == ParserState::FetchingNull) {
                result.error = ParserError::UnknownArgument;
            } else if(result.state == ParserState::FetchingFileName) {
                fileName = word;
                result.state = ParserState::FetchingNull;
            } else if(result.state == ParserState::FetchingDirName) {
                dirName = word;
                result.state = ParserState::FetchingNull;
            }
        } while(true);

        return result;
    }

    void connect_to(const string& connect_to_addr, uint16_t connect_port) {
        LOG(INFOBOLD, "Connecting to {} on port {}", connect_to_addr, connect_port);
        m_client.Connect(connect_to_addr, connect_port);
        if (m_client.IsConnected()) {
            LOG(INFOBOLD, "Successfully connected.");
        } else {
            LOG(ERR, "Failed to connect.");
        }
    }

private:
    sw::Socket m_socket;
    sw::Socket m_client;
    list<sw::Socket> m_clients;
};

void test_file_transfer() {
    file_transfer_host host;
    host.start_terminal_interface();
    this_thread::sleep_for(chrono::hours(10));
}
