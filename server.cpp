#include <iostream>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <algorithm>
#include <memory>

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

#define MAX_CLIENTS 100
#define MAX_ID_LEN 10
#define MAX_TOPIC_LEN 50
#define MAX_UDP_PAYLOAD 1500
#define UDP_MSG_BASE_SIZE (MAX_TOPIC_LEN + 1) // dimensiunea fixa a header-ului UDP (topic + tip)

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

// structura mesaj TCP (trimis de server -> client)
// format: [lungime utila (4 bytes)][datele de mai jos...]
#pragma pack(push, 1) // asigura alinierea la 1 byte
struct TcpUdpMessage
{
    // NU includem lungimea aici, e trimisa separat inainte ca prefix
    struct in_addr udp_ip;         // IP client UDP (network byte order)
    uint16_t udp_port;             // port client UDP (network byte order)
    char topic[MAX_TOPIC_LEN + 1]; // topic-ul mesajului UDP
    uint8_t data_type;             // tipul de date din mesajul UDP
    // continutul (payload-ul) mesajului UDP este trimis imediat dupa aceasta structura
    // lungimea continutului este data de: lungime utila - sizeof(TcpUdpMessage)
};
#pragma pack(pop)

/**
 * @brief trimite toate datele specificate pe socket.
 * gestioneaza trimiterile partiale.
 * @param sockfd socket descriptor.
 * @param buf bufferul cu date de trimis.
 * @param len lungimea datelor de trimis.
 * @return 0 la succes, -1 la eroare sau conexiune inchisa.
 */
int send_all(int sockfd, const void *buf, size_t len) {
    size_t total_sent = 0;
    const char *buffer = (const char *)buf;
    while (total_sent < len) {
        // folosim MSG_NOSIGNAL pentru a preveni SIGPIPE daca clientul inchide conexiunea brusc
        int bytes_sent = send(sockfd, buffer + total_sent, len - total_sent, MSG_NOSIGNAL);
        if (bytes_sent <= 0) {
            return -1;
        }
        total_sent += bytes_sent;
    }
    return 0; // succes
}

/**
 * @brief imparte un string dupa un delimitator si ignora token-urile goale
 * @param str -
 * @param delimiter -
 * @return vectorul de string-uri
 */
vector<string> split(const string &str, char delimiter) {
    vector<string> tokens;
    string token;
    istringstream tokenStream(str);
    while (getline(tokenStream, token, delimiter)) {
        // ignoram pe cele goale
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

/**
 * @brief functie recursiva pentru potrivirea unui topic de mesaj cu un pattern de abonament
 * @param sub_parts vector cu partile patternului de abonament
 * @param sub_idx indexul curent in pattern
 * @param msg_parts vector cu partile topicului mesajului
 * @param msg_idx indexul curent in topicul mesajului
 * @return true daca se potriveste, false altfel
 */
bool its_a_match(const vector<string> &sub_parts, size_t sub_idx,
                     const vector<string> &msg_parts, size_t msg_idx) {
    // am ajuns la finalul patternului de abonament
    if (sub_idx == sub_parts.size()) {
        // se potriveste doar daca am ajuns si la finalul topicului mesajului
        return msg_idx == msg_parts.size();
    }

    const string &sub_token = sub_parts[sub_idx];

    // gestionare wildcard '*'
    if (sub_token == "*") {
        // opt 1: '*' potriveste zero segmente; incercam sa potrivim restul patternului cu segmentul curent al mesajului
        if (its_a_match(sub_parts, sub_idx + 1, msg_parts, msg_idx)) {
            return true;
        }
        // opt 2: '*' potriveste unul sau mai multe segmente
        // daca mesajul nu s-a terminat, incercam sa potrivim '*' cu segmentul curent al mesajului
        // si pastram acelasi '*' pentru urmatorul segment al mesajului
        if (msg_idx < msg_parts.size()) {
            if (its_a_match(sub_parts, sub_idx, msg_parts, msg_idx + 1)) {
                return true;
            }
        }

        return false;
    }

    // am ajuns la finalul topicului mesajului, dar nu si al patternului(si patternul nu a fost '*'(gestionat mai sus))
    if (msg_idx == msg_parts.size()) {
        // patternul mai are parti non '*' ramase, dar mesajul s-a terminat, deci nu se potriveste
        return false;
    }

    // gestionare '+'
    if (sub_token == "+") {
        // '+' trebuie sa potriveasca exact un segment, deci avansam si in pattern si in mesaj
        return its_a_match(sub_parts, sub_idx + 1, msg_parts, msg_idx + 1);
    }

    // perfect match
    if (sub_token == msg_parts[msg_idx]) {
        // segmentele se potrivesc, deci vansam si in pattern si in mesaj
        return its_a_match(sub_parts, sub_idx + 1, msg_parts, msg_idx + 1);
    }

    return false;
}

/**
 * @brief verifica daca un topic de mesaj se potriveste cu un topic de abonament(cu wildcard)
 * @param subscription_topic topic-ul de abonament (poate contine + / *)
 * @param message_topic topic-ul mesajului primit
 * @return true daca se potriveste, false altfel
 */
bool topic_matches(const string &subscription_topic, const string &message_topic) {
    // abonament la '*' se potriveste cu orice topic
    if (subscription_topic == "*")
        return true;

    // daca abonamentul nu are wildcard-uri, facem comparare directa
    if (subscription_topic.find('+') == string::npos && subscription_topic.find('*') == string::npos) {
        return subscription_topic == message_topic;
    }

    vector<string> sub_parts = split(subscription_topic, '/');
    vector<string> msg_parts = split(message_topic, '/');

    if (msg_parts.empty() && !sub_parts.empty())
        return false;
    if (sub_parts.empty() && !msg_parts.empty())
        return false;
    if (sub_parts.empty() && msg_parts.empty())
        return true;

    return its_a_match(sub_parts, 0, msg_parts, 0);
}

/**
 * @brief curata resursele asociate unui client deconectat
 * pastreaza abonamentele persistente, dar sterge FD-ul din abonamentele active
 * @param client_fd file descriptor-ul clientului
 * @param fd_to_id mapare FD -> ID
 * @param id_to_fd mapare ID -> FD(doar pt clienti conectati)
 * @param connected_ids set de ID-uri conectate
 * @param persistent_subscriptions mapare ID -> set<Topic>(abonamente persistente)
 * @param topic_subscriptions mapare topic -> set<FD>(abonamente active)
 */
void cleanup_client(int client_fd,
                    unordered_map<int, string> &fd_to_id,
                    unordered_map<string, int> &id_to_fd,
                    unordered_set<string> &connected_ids,
                    unordered_map<string, unordered_set<string>> &persistent_subscriptions,
                    unordered_map<string, unordered_set<int>> &topic_subscriptions) {
    if (fd_to_id.count(client_fd)) {
        string cid = fd_to_id[client_fd];
        cout << "Client " << cid << " disconnected." << endl;

        // sterge maparile legate de conexiunea activa(FD)
        connected_ids.erase(cid);
        id_to_fd.erase(cid);

        // sterge FD-ul din abonamentele active(topic_subscriptions)
        // iteram prin abonamentele PERSISTENTE ale acestui ID pentru a sti ce topicuri sa verificam
        if (persistent_subscriptions.count(cid)) {
            for (const string &topic : persistent_subscriptions.at(cid)) {
                if (topic_subscriptions.count(topic)) {
                    topic_subscriptions[topic].erase(client_fd);// sterge doar FD-ul inactiv
                    if (topic_subscriptions[topic].empty()) {
                        topic_subscriptions.erase(topic);
                    }
                }
            }
        }

        fd_to_id.erase(client_fd);// sterge maparea FD->ID la final
    }

    close(client_fd);
}

int main(int argc, char **argv) {
    DIE(argc != 2, "usage: ./server <PORT>");
    setvbuf(stdout, NULL, _IONBF, BUFSIZ); // dezactivare buffering stdout

    int port = atoi(argv[1]);
    DIE(port <= 0 || port > 65535, "invalid port number!\n");

    // creare socket tcp
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    DIE(tcp_socket < 0, "socket tcp!\n");
    int opt = 1;

    // permite reutilizarea adresei imediat dupa inchiderea serverului
    DIE(setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) < 0, "setsockopt tcp SO_REUSEADDR failed");

    // creare socket udp
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(udp_socket < 0, "socket udp!\n");

    // adresa server
    sockaddr_in serv_addr{};
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    // bind pentru tcp si udp
    DIE(bind(tcp_socket, (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0, "bind tcp!\n");
    DIE(bind(udp_socket, (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0, "bind udp!\n");

    // ascultare conexiuni tcp
    DIE(listen(tcp_socket, MAX_CLIENTS) < 0, "listen tcp!\n");

    vector<pollfd> poll_fds;
    poll_fds.push_back({.fd = STDIN_FILENO, .events = POLLIN});// pentru stdin
    poll_fds.push_back({.fd = tcp_socket, .events = POLLIN});// pentru conexiuni TCP noi
    poll_fds.push_back({.fd = udp_socket, .events = POLLIN});// pentru mesaje UDP

    // structuri de date folositoare
    unordered_map<int, string> fd_to_id; // mapare FD -> ID(doar pt clienti conectati)
    unordered_map<string, int> id_to_fd; // mapare ID -> FD (doar pt clienti conectati)
    unordered_set<string> connected_ids; // set ID-uri conectate
    // mapare ID -> set<topic>(abonamente persistente, cheia e ID-ul clientului)
    unordered_map<string, unordered_set<string>> persistent_subscriptions;
    // mapare topic -> set<FD>(abonamente active, cheia e topicul/patternul)
    unordered_map<string, unordered_set<int>> topic_subscriptions;

    bool server_running = true;
    // server loop
    while (server_running) {
        int ret = poll(poll_fds.data(), poll_fds.size(), -1);
        DIE(ret < 0, "poll!\n");

        // vector pentru a colecta FD-urile ce trebuie curatate la sfarsitul acestei iteratii
        vector<int> fds_to_cleanup_this_iteration;

        for (size_t i = 0; i < poll_fds.size(); i++) {
            if (!(poll_fds[i].revents & POLLIN)) {
                continue; // ffara eveniment de citire pe acest fd
            }

            // comanda de la tatsatura pentru server
            if (poll_fds[i].fd == STDIN_FILENO) {
                string cmd;
                getline(cin, cmd);
                if (cin.eof() || cmd == "exit") {
                    cout << "Server shutting down.\n";
                    server_running = false;
                    for (const auto &pair : fd_to_id) {
                        bool already_marked_exit = false;
                        for (int fd_clean : fds_to_cleanup_this_iteration)
                            if (fd_clean == pair.first)
                                already_marked_exit = true;

                        if (!already_marked_exit)
                            fds_to_cleanup_this_iteration.push_back(pair.first);
                    }
                    break;
                } else {
                    cerr << "Unknown server command: " << cmd << endl;
                }

                // o noua conexiune TCP
            } else if (poll_fds[i].fd == tcp_socket) {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                // accepta noua conexiune
                int new_client_fd = accept(tcp_socket, (sockaddr *)&client_addr, &client_len);
                if (new_client_fd < 0) {
                    perror("accept failed!\n");
                    continue;
                }

                // dezactivare algoritm Nagle
                int flag = 1;
                if (setsockopt(new_client_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)) < 0) {
                    perror("setsockopt TCP_NODELAY failed for new client!\n");
                    close(new_client_fd);
                    continue;
                }

                // primeste ID-ul clientului(maxim 10 caractere)
                char id_buf[MAX_ID_LEN + 1] = {0};
                int id_len = recv(new_client_fd, id_buf, MAX_ID_LEN, 0);
                if (id_len <= 0) {
                    if (id_len < 0)
                        perror("recv client ID failed");
                    close(new_client_fd);
                    continue;
                }
                id_buf[id_len] = '\0';
                string client_id(id_buf);

                // verifica daca ID-ul este deja conectat
                if (connected_ids.count(client_id)) {
                    cout << "Client " << client_id << " already connected." << endl;
                    string err_msg = "Client " + client_id + " already connected.\n";
                    send_all(new_client_fd, err_msg.c_str(), err_msg.length());
                    close(new_client_fd);
                } else {
                    // client nou sau reconectare cu un ID valid
                    connected_ids.insert(client_id);
                    fd_to_id[new_client_fd] = client_id;
                    id_to_fd[client_id] = new_client_fd;
                    // adauga noul FD la structura poll
                    poll_fds.push_back({.fd = new_client_fd, .events = POLLIN});

                    cout << "New client " << client_id << " connected from "
                         << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << ".\n";

                    // reactiveaza abonamentele persistente pentru acest ID
                    if (persistent_subscriptions.count(client_id)) {
                        for (const string &topic : persistent_subscriptions.at(client_id))
                            topic_subscriptions[topic].insert(new_client_fd);
                    }
                }

            // mesaj UDP
            } else if (poll_fds[i].fd == udp_socket) {
                char buffer[BUFSIZ];
                sockaddr_in udp_client_addr{};
                socklen_t client_len = sizeof(udp_client_addr);

                int len = recvfrom(udp_socket, buffer, BUFSIZ, 0, (sockaddr *)&udp_client_addr, &client_len);
                if (len < 0) {
                    perror("recvfrom udp!\n");
                    continue;
                }

                // validare lungime minima (topic + tip data)
                if (len < UDP_MSG_BASE_SIZE) {
                    cerr << "Warning: Received UDP message too short (" << len << " bytes) from "
                         << inet_ntoa(udp_client_addr.sin_addr) << ":" << ntohs(udp_client_addr.sin_port) << endl;
                    continue;
                }

                // -parsare mesaj UDP
                char topic_arr[MAX_TOPIC_LEN + 1] = {0};
                memcpy(topic_arr, buffer, MAX_TOPIC_LEN);
                topic_arr[MAX_TOPIC_LEN] = '\0';
                string message_topic_str(topic_arr, strnlen(topic_arr, MAX_TOPIC_LEN));

                uint8_t data_type = buffer[MAX_TOPIC_LEN];
                const char *content_payload = buffer + UDP_MSG_BASE_SIZE;
                int content_len = len - UDP_MSG_BASE_SIZE;

                // pregateste header-ul mesajului pentru trimitere TCP
                TcpUdpMessage msg_header;
                memset(&msg_header, 0, sizeof(msg_header));
                msg_header.udp_ip = udp_client_addr.sin_addr;   // pastram network byte order
                msg_header.udp_port = udp_client_addr.sin_port; // same
                strncpy(msg_header.topic, message_topic_str.c_str(), MAX_TOPIC_LEN);
                msg_header.topic[MAX_TOPIC_LEN] = '\0';
                msg_header.data_type = data_type;

                // calculeaza dimensiunile pentru protocolul TCP
                size_t header_size = sizeof(TcpUdpMessage);
                // lungimea utila = dimensiunea headerului TCP + lungimea continutului UDP
                uint32_t total_payload_len = header_size + content_len;
                // converteste lungimea la network byte order pentru prefix
                uint32_t net_payload_len = htonl(total_payload_len);

                // va contine clientii TCP activi abonati la acest topic
                unordered_set<int> target_fds;

                for (const auto &sub_entry : persistent_subscriptions) {
                    const string &subscriber_id = sub_entry.first;
                    // verifica daca ID-ul este conectat in acest moment
                    if (id_to_fd.count(subscriber_id)) {
                        int active_fd = id_to_fd.at(subscriber_id);// obtine FD-ul activ
                        const auto &topics_for_id = sub_entry.second;// obtine topicurile la care e abonat ID-ul
                        // verifica fiecare topicla care e abonat ID-ul
                        for (const string &subscribed_topic : topics_for_id) {
                            if (topic_matches(subscribed_topic, message_topic_str)) {
                                target_fds.insert(active_fd);
                            }
                        }
                    }
                }

                // trimite mesajul clientilor TCP gasiti
                if (!target_fds.empty()) {
                    // construieste bufferul complet de trimitere : [lungime_prefix][header][continut]
                    vector<char> send_buffer(sizeof(net_payload_len) + total_payload_len);
                    // copiaza prefixul de lungime
                    memcpy(send_buffer.data(), &net_payload_len, sizeof(net_payload_len));
                    // copiaza header-ul TCP
                    memcpy(send_buffer.data() + sizeof(net_payload_len), &msg_header, header_size);
                    // copiaza continutul UDP
                    memcpy(send_buffer.data() + sizeof(net_payload_len) + header_size, content_payload, content_len);

                    // trimite bufferul la fiecare client
                    for (int target_fd : target_fds) {
                        if (fd_to_id.count(target_fd)) {
                            if (send_all(target_fd, send_buffer.data(), send_buffer.size()) < 0) {
                                bool already_marked_send = false;
                                for (int fd_clean : fds_to_cleanup_this_iteration)
                                    if (fd_clean == target_fd)
                                        already_marked_send = true;
                                if (!already_marked_send) {
                                    cerr << "Error sending UDP relay to client FD " << target_fd << ". Marking for cleanup." << endl;
                                    fds_to_cleanup_this_iteration.push_back(target_fd);
                                }
                            }
                        }
                    }
                }

            // comanda de la client TCP
            } else {
                int client_fd = poll_fds[i].fd;
                char buffer[BUFSIZ];
                int len = recv(client_fd, buffer, BUFSIZ - 1, 0);

                if (len <= 0) {
                    if (len < 0)
                        perror("recv from tcp client failed");

                    bool already_marked_recv = false;
                    for (int fd_clean : fds_to_cleanup_this_iteration)
                        if (fd_clean == client_fd)
                            already_marked_recv = true;
                    if (!already_marked_recv)
                        fds_to_cleanup_this_iteration.push_back(client_fd);
                    continue;
                }

                // len > 0 : clientul a trimis date
                buffer[len] = '\0';
                string command_line(buffer);
                // proceseaza potentiale comenzi multiple separate prin \n
                stringstream cmd_stream(command_line);
                string single_cmd;

                if (!fd_to_id.count(client_fd))
                    continue;

                string client_id = fd_to_id.at(client_fd);

                while (getline(cmd_stream, single_cmd, '\n')) { // line by line
                    stringstream ss(single_cmd);
                    string command, topic;
                    ss >> command; // extrage primul cuvant(comanda)

                    if (command == "subscribe") {
                        // folosim getline pentru a lua restul liniei ca topic, gestionand spatiile
                        getline(ss, topic);
                        // elimina spatiul de la inceput ramas de la '>>'
                        if (!topic.empty() && topic.front() == ' ') {
                            topic.erase(0, 1);
                        }

                        if (!topic.empty() && topic.length() <= MAX_TOPIC_LEN) {
                            // adauga la abonamentele persistente(ID -> Topic)
                            persistent_subscriptions[client_id].insert(topic);
                            // adauga FD-ul curent la abonamentele active(Topic -> FD)
                            topic_subscriptions[topic].insert(client_fd);
                        } else {
                            cerr << "Warning: Invalid subscribe topic from " << client_id << ": [" << topic << "]" << endl;
                        }
                    } else if (command == "unsubscribe") {// dezabonarea, atat din cele persistente cat si din cele active
                        getline(ss, topic);
                        if (!topic.empty() && topic.front() == ' ') {
                            topic.erase(0, 1);
                        }

                        if (!topic.empty() && topic.length() <= MAX_TOPIC_LEN) {
                            persistent_subscriptions[client_id].erase(topic);
                            if (topic_subscriptions.count(topic)) {
                                topic_subscriptions[topic].erase(client_fd);
                                if (topic_subscriptions[topic].empty()) {
                                    topic_subscriptions.erase(topic);
                                }
                            }
                        } else {
                            cerr << "Warning: Invalid unsubscribe topic from " << client_id << ": [" << topic << "]" << endl;
                        }
                    } else if (command == "exit") {
                        bool already_marked_exit_cmd = false;
                        for (int fd_clean : fds_to_cleanup_this_iteration)
                            if (fd_clean == client_fd)
                                already_marked_exit_cmd = true;
                        if (!already_marked_exit_cmd)
                            fds_to_cleanup_this_iteration.push_back(client_fd);
                        break;
                    } else {
                        cerr << "Warning: Unknown command from " << client_id << ": " << single_cmd << endl;
                    }
                }
            }
        }

        // cleanup FDs marcati in aceasta iteratie
        if (!fds_to_cleanup_this_iteration.empty()) {
            vector<pollfd> next_poll_fds;
            // construieste noul vector poll_fds fara descriptorii marcati
            for (const auto &pfd : poll_fds) {
                bool should_remove = false;
                for (int fd_clean : fds_to_cleanup_this_iteration) {
                    if (pfd.fd == fd_clean) {
                        cleanup_client(fd_clean, fd_to_id, id_to_fd, connected_ids, persistent_subscriptions, topic_subscriptions);
                        should_remove = true;
                        break;
                    }
                }
                // adauga in noul vector doar daca NU trebuie sters
                if (!should_remove) {
                    next_poll_fds.push_back(pfd);
                }
            }
            poll_fds = std::move(next_poll_fds); // actualizeaza vectorul poll_fds eficient
        }
    }

    close(tcp_socket);
    close(udp_socket);

    return 0;
}
