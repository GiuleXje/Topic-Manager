#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>

using namespace std;

#define DIE(assertion, call_description)  \
    do                                    \
    {                                     \
        if (assertion)                    \
        {                                 \
            fprintf(stderr, "(%s, %d): ", \
                    __FILE__, __LINE__);  \
            perror(call_description);     \
            exit(EXIT_FAILURE);           \
        }                                 \
    } while (0)

#define MAX_ID_LEN 10
#define MAX_TOPIC_LEN 50
#define MAX_UDP_PAYLOAD 1500

// structura Mesaj TCP(primit de la Server)
#pragma pack(push, 1)
struct TcpUdpMessage {
    struct in_addr udp_ip;
    uint16_t udp_port;
    char topic[MAX_TOPIC_LEN + 1];
    uint8_t data_type;
};
#pragma pack(pop)

int send_all(int sockfd, const void *buf, size_t len) {
    size_t total_sent = 0;
    const char *buffer = (const char*) buf;
    while (total_sent < len) {
        int bytes_sent = send(sockfd, buffer + total_sent, len - total_sent, 0);
        if (bytes_sent <= 0)
            return -1;
        total_sent += bytes_sent;
    }
    return 0;
}

void parse_and_print_message(const TcpUdpMessage* msg_header, const char* content_payload, size_t content_len) {
    string data_type_str;
    string value_str;
    stringstream ss;

    switch (msg_header->data_type) {
        case 0: // INT
            {
                data_type_str = "INT";
                if (content_len >= 1 + sizeof(uint32_t)) {
                    uint8_t sign = content_payload[0];
                    uint32_t num_net;
                    memcpy(&num_net, content_payload + 1, sizeof(uint32_t));
                    uint32_t num_host = ntohl(num_net);
                    if (sign == 1 && num_host != 0) {
                        ss << "-";
                    }
                    ss << num_host;
                    value_str = ss.str();
                } else {
                    value_str = "[invalid INT payload]";
                }
            }
            break;
        case 1: // SHORT_REAL
            {
                data_type_str = "SHORT_REAL";
                 if (content_len >= sizeof(uint16_t)) {
                    uint16_t num_net;
                    memcpy(&num_net, content_payload, sizeof(uint16_t));
                    uint16_t num_host = ntohs(num_net);
                    double real_val = static_cast<double>(num_host) / 100.0;
                    ss << fixed << setprecision(2) << real_val;
                    value_str = ss.str();
                 } else {
                    value_str = "[invalid SHORT_REAL payload]";
                }
            }
            break;
        case 2: // FLOAT
            {
                data_type_str = "FLOAT";
                size_t expected_len = 1 + sizeof(uint32_t) + sizeof(uint8_t);
                if (content_len >= expected_len) {
                    uint8_t sign = content_payload[0];
                    uint32_t num_abs_net;
                    memcpy(&num_abs_net, content_payload + 1, sizeof(uint32_t));
                    uint32_t num_abs_host = ntohl(num_abs_net);
                    uint8_t power_neg_10 = content_payload[1 + sizeof(uint32_t)];

                    double real_val = static_cast<double>(num_abs_host);
                    if (power_neg_10 > 0) {
                        real_val /= pow(10.0, power_neg_10);
                    }

                    if (sign == 1 && num_abs_host != 0) {
                        ss << "-";
                    }
                    ss << fixed << setprecision(power_neg_10) << real_val;
                    value_str = ss.str();
                 } else {
                    value_str = "[invalid FLOAT payload]";
                }

            }
            break;
        case 3: // STRING
            {
                data_type_str = "STRING";
                value_str.assign(content_payload, content_len);
            }
            break;
        default:
            data_type_str = "UNKNOWN(" + to_string(msg_header->data_type) + ")";
            value_str = "[raw data]";
            break;
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(msg_header->udp_ip), ip_str, INET_ADDRSTRLEN);

    cout << ip_str << ":" << ntohs(msg_header->udp_port)
         << " - " << msg_header->topic << " - " << data_type_str << " - " << value_str << endl;

}


int main(int argc, char **argv) {

    DIE(argc != 4, "usage: ./subscriber <ID_CLIENT> <IP_SERVER> <PORT_SERVER>");
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    string client_id = argv[1];
    DIE(client_id.length() > MAX_ID_LEN, "client ID too long!\n");

    char *server_ip = argv[2];
    int server_port = atoi(argv[3]);
    DIE(server_port <= 0 || server_port > 65535, "invalid server port!\n");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "socket!\n");

    int flag = 1;
    int ret_setsockopt = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
    DIE(ret_setsockopt < 0, "setsockopt TCP_NODELAY!\n");

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    ret_setsockopt = inet_aton(server_ip, &serv_addr.sin_addr);
    DIE(ret_setsockopt == 0, "inet_aton!\n");

    int ret_connect = connect(sockfd, (sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret_connect < 0) {
        perror("connect failed!\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    int ret_send = send_all(sockfd, client_id.c_str(), client_id.length());
    DIE(ret_send < 0, "send client ID!\n");

    pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = sockfd;
    pfds[1].events = POLLIN;

    vector<char> recv_buffer;

    while (true) {
        int ret_poll = poll(pfds, 2, -1);
        DIE(ret_poll < 0, "poll failed!\n");

        // input de la tastatura
        if (pfds[0].revents & POLLIN) {
            string line;
            getline(cin, line);

            if (cin.eof() || line == "exit") {
                break;
            }

            stringstream ss(line);
            string command;
            string topic;
            ss >> command;

            string command_to_send = line + "\n";

            if (command == "subscribe") {
                getline(ss, topic);
                if (!topic.empty() && topic.front() == ' ')
                    topic.erase(0, 1);

                if (!topic.empty()) {
                    ret_send = send_all(sockfd, command_to_send.c_str(), command_to_send.length());
                    if (ret_send < 0) {
                        break;
                    }
                    cout << "Subscribed to topic " << topic << ".\n";
                } else {
                    cerr << "Invalid topic specified for subscribe.\n";
                }
            } else if (command == "unsubscribe") {
                getline(ss, topic);
                if (!topic.empty() && topic.front() == ' ')
                    topic.erase(0, 1);

                if (!topic.empty()) {
                    ret_send = send_all(sockfd, command_to_send.c_str(), command_to_send.length());
                    if (ret_send < 0) {
                        break;
                    }
                    cout << "Unsubscribed from topic " << topic << ".\n";
                 } else {
                    cerr << "Invalid topic specified for unsubscribe.\n";
                }
            } else {
                cerr << "Unknown command: " << line << endl;
            }
        }

        // date primite de la server
        if (pfds[1].revents & POLLIN) {
            char temp_buf[BUFSIZ];
            int bytes_received = recv(sockfd, temp_buf, BUFSIZ, 0);

            if (bytes_received <= 0) {
                if (bytes_received < 0)
                    perror("recv failed");
                break;
            }

            recv_buffer.insert(recv_buffer.end(), temp_buf, temp_buf + bytes_received);

            size_t current_offset = 0; // pozitia in buffer
            while (recv_buffer.size() - current_offset >= sizeof(uint32_t)) {
                uint32_t expected_payload_len_net;
                memcpy(&expected_payload_len_net, recv_buffer.data() + current_offset, sizeof(uint32_t));
                uint32_t expected_payload_len_host = ntohl(expected_payload_len_net);

                size_t header_size = sizeof(TcpUdpMessage);
                size_t required_total_size = sizeof(uint32_t) + expected_payload_len_host;

                // verificam daca avem suficiente date pentru intregul pachet
                if (recv_buffer.size() - current_offset >= required_total_size) {
                    // mesaj complet

                    // verificam daca lungimea payload-ului e cel putin cat header-ul
                    if (expected_payload_len_host < header_size) {
                         cerr << "Error: Received payload length (" << expected_payload_len_host
                              << ") is smaller than header size (" << header_size << "). Skipping packet.\n";
                         // sarim peste acest pachet invalid
                         current_offset += required_total_size;
                         continue;
                    }

                    TcpUdpMessage msg_header;
                    const char* buffer_start = recv_buffer.data() + current_offset;
                    memcpy(&msg_header,
                           buffer_start + sizeof(uint32_t),
                           header_size);

                    const char* content_payload = buffer_start + sizeof(uint32_t) + header_size;
                    size_t content_len = expected_payload_len_host - header_size;
                    msg_header.topic[MAX_TOPIC_LEN] = '\0';

                    parse_and_print_message(&msg_header, content_payload, content_len);

                    // avansam offsetul cu dimensiunea totala a pachetului procesat
                    current_offset += required_total_size;

                } else {
                    break;
                }
            }

            if (current_offset > 0)
                recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + current_offset);
        }
    }

    close(sockfd);
    return 0;
}
