# Tema 2 - Apicatie Client - Server

    Aceasta aplicatie implemneteaza un sistem client server pentru abonarea
unor clienti TCP la topicuri transmise de catre clientii UDP.
    Aplicatia este compusa din 2 componente principale:
## 1) Server

    Server-ul asculta pe un port specificat, permitand atat conexiuni TCP,
ce sunt destinate abonatilor nostri, cat si conexiuni UDP, prin care vor fi
transmise topicurile ce sunt de interes pentru abonatii TCP. Server-ul va
gestiona conexiunile abonatilor TCP, permitand conectarea, deconectarea,
reconectarea, iar fiecare client va fi identificat pe baza unui ID unic.
    Topicurile de care un abonat este interesat vor fi gestionate pe baza
comenzilor de "subscribe" si "unsubscribe", ce vor marca interesul/dezinteresul
pentru un anume subiect. Am avut grija in implementare sa tin cont si de faptul
ca desi un client se poate deconecta, cand acesta va dori sa se reconecteze va
trebui sa ramana abonat la toate topicurile de interes pentru el. Asadar, am
folosit o structura map pentru a retine toate topicurile la care un utilizator
a dat comanda subscribe(acest map va ramane nemodificat pana la inchiderea
serverului, sau pana cand un abonat decide ca un anume subiect nu mai este de
interes). Evident topicurile pot contine wildcard-uri, si am avut grija sa tin
cont de faptul ca '*' poate inlocui mai multe nivele, iar '+' doar unul.
    Mesajele primite prin UDP(topicurile), vor fi parsate, si apoi redirectionate
prin protocolul peste TCP(voi intra in amanunt despre el mai jos) catre toti
clientii activi(conectati in acel moment), care sunt abonati la acel topic.
    In plus, comanda "exit" marcheaza inchidere serverului.

## 2) Subscriber

    Subscriberii sunt reprezentati de clientii TCP, care se vor conecta la
server pe baza unui IP unic. Acesti abonati vor putea alege pe baza comenzilor
de "subscirbe" sau "unsubscribe" citite de la tastatura, topicurile ce reprezinta
un interes pentru ei. Aceasta cerere este trimisa catre server, care va modifica
strucurile de date ce gestioneaza atat abonarea persistenta(ce va arata subscriptiile
unui abonat, dupa ce acesta se va reconecta), cat si pe cele active.
    In plus, pentru fiecare topic la care utilizatorul este abonat, si pentru care
se primeste un mesaj din partea clientilor UDP, se va afisa pentru fiecare client
TCP abonat, un mesaj de forma:
    <IP_CLIENT_UDP>:<PORT_CLIENT_UDP> - <TOPIC> - <TIP_DATE> - <MESAJ>


## Protocolul peste TCP

    Pentru a asigura o comunicare eficiente si solida intre server si clientii
TCP, am implementat urmatorul protocol, alcatuit din urmatoarele:

### 1) Incadrarea mesajelor

- fiecare mesaj trimis catre server este precedat de un prefix de lungime, care
are o dimensiune fixa de 4 bytes
- valoarea trimisa in prefix reprezinta lungimea totala a datelor utile care
vor urma, adica dimensiunea header-ului TPC + dimensiunea continutului original
UDP
- lungimea este trimisa in Big Endian folosind htonl si citita folosind ntohl
- clientul citeste intai cei 4 octeti ai prefixului, iar odata obtinuta lungimea L,
incearca sa citeasca exact L octeti din fluxul TCP; daca recv returneaza mai putin
de L octeti, clientul va stoca datele primite intr-un buffer intern si va continua
sa citeasca pana cand va obtine pe toti cei L octeti necesari. Daca totusi dupa citire
dimensiunea bufferului trece de L bytes, surplusul va fi pastrat in buffer, deoarece
reprezinta o parte a unui mesaj viitor, inca incomplet. Acest mecanism asigura
separarea corecta a mesajelor, indiferent de cum le fragmenteaza/concateneaza TCP

### 2) Structura mesajului

- imediat dupa prefixul de lungime, serverul trimite datele, compuse din 2 parti:
1) Header (TcpUdpMessage) - structura de contine toate metadatele necesare:
* udp_ip : adresa IP sursa a clientului UDP(in network byte order)
* udp_port: portul clientului UDP
* topic: topic-ul mesajului original UDP
* data_type: tipul de date al continutului(0=INT, 1=SHORT_REAL,2=FLOAT, 3=STRING)

2) Continut

- datele din mesajul UDP, trimise imediat dupa mesajul headerul TCP; lungimea
acestui continut este calculata de client ca (lungimea totala utila) - sizeof(TcpUdpMessage)


    In plus, ne asiguram ca atat pentru server cat si pentru client dezactivam
algoritmul Nagle, asigurandu-ne ca pachetele TCP mici sunt trimise cat mai repede,
fara a astepta acumularea mai multor date in buffer, mentinand astfel o latenta
scazuta, importanta pentru rate mari de mesaje.

    Serverul trimite mesajele doar catre clientii TCP abonati si care sunt conectati
in acel moment. Acest lucru se face prin folosirea structurilor "persistent_subscriptions" si
"id_to_fd", pentru a gasi corect destinatarii acestui mesaj, evitand astfel trimiterea
acestui mesaj catre clienti care nu sunt abonati sau inactivi(deconectati).

    Acest protocol asigura o delimitare limpede a a mesajelor in fluxul TCP, permitand
transmiterea eficienta si favorizand o comunicare cu latenta scazuta intre server si
abonati.

