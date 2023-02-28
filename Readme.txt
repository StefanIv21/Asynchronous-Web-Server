==========================Tema 3SO==========================

Ionescu Valentin Stefan
322CC

implementare fara fisiere dinamice (fara operatii I/O asincrone (libaio))
am pornit de la structura "epoll_echo_server.c"
am adaugat functii si structuri din "test_get_request_path.c"
        functia de callback (on_path_cp)
        structura "http_parser_settings setting on path"


in structura connection am adaugat:
        variabila err : retin daca  http requestul trimis de socket contine  un fisier inexistent
        variabila noufd : descriptor pentru fisierul trimis prin request 
        variabila filesize : retin dimensiunea fisierului
        variabila after_parser: retin daca a fost parsat http requestul trimis de socket
        structura stat buffer : folosita pentru a retine dimensiunea fisierului (folosita de functia stat)


in functia handle_new_connection am adaugat functia fcntl pentru a face un socket sa fie non-blocant

functia receive_message:
        -verific daca requestul pentru socket a fost deja parsat(in caz afirmativ ies din functie)
        -citesc din socket folosind functia recv si pun in vectorul recv_buffer
        -daca recv_buffer contine doua newline uri ("\r\n\r\n") atunci parsez requestul facut
        -pun in variabila path calea relativa de la request path
        -incerc sa deschid fisierul cu path creat
                :daca se deschide fisierul,pun in vectorul send_buffer mesajul ca fisierul este existent si aflu dimensiunea lui
                :daca nu se deschide fisierul,modific variabila err si pun in vectorul send_buffer mesajul ca fisierul este inexistent
        -daca http requestul a fost unul valid(a continut "\r\n\r\n") returnez mesajul "STATE_DATA_RECEIVED"
    

functia handle_client_request:
            folosesc functia receive_message
            daca valoarea de return este "STATE_DATA_RECEIVED", atunci adaug socketul pentru scriere ("w_epoll_update_ptr_inout")

functia send_messaga:
        indiferent de mesajul din send_buffer il trimit pe socket(functia send)
        daca mesajul a fost cel de eroare(fisier invalid) opresc conexiunea cu socketul
        dupa ce am trimis tot mesajul ca exista fisierul cerut, trimit continutul fisierului cu zero-copy (sendfile)
            dupa fiecare folosire a functiei sendfile, scad din dimeniunea fisierului valoarea de return a functiei sendfile
            schimb de fiecare data offsetul fisierului folosind functia lseek
	    cand numarul de bytes cititi este egala cu numarul de bytes din fisier
                     (adica am trimis tot continutul) opresc conexiunea cu socketul
            


tema a fost utila:Da 
implementare: se putea mai bine
cum se compilează :folosind comanda make run 
                  :headere folosite aws.h sock_util.h http_parser.h debug.h util.h w_epoll.h

cum se ruleaza: fara argumente executabilul ./aws
              : in alt terminal se trimite un http request catre server
 
                




