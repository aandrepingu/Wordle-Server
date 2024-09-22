#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <setjmp.h>
#include "word_storage.h"

#define MAX_CLIENTS 128
#define MAXBUFFER 8192

extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char **words;
int hidden_words_counter;

// stores all words in the game
char **words_list;
int num_words;
// // rng seed
// unsigned int seed;

// locks for guesses + win/loss/words editing
pthread_mutex_t guess_lock, round_end_lock, words_lock;

int signal_received;
int server_socket;
void *handle_connection(void *fds);                      // thread plays wordle game here
int valid_guess(char *word, char *buffer, char *actual); // check if word is a valid word against words_list


/**
 * Signal handler which shuts down the server when receiving SIGUSR1
 */
void signal_handler(int sig)
{
    if (sig == SIGUSR1)
    {
        // TODO: terminate running threads and free up memory
        printf("MAIN: SIGUSR1 rcvd; Wordle server shutting down...\n");
        for (int i = 0; i < num_words; i++)
        {
            free(*(words_list + i));
        }
        free(words_list);
        signal_received = 1;
        close(server_socket);
        // server_socket = -1;
    }
}


int wordle_server(int argc, char **argv)
{
    setvbuf( stdout, NULL, _IONBF, 0 );
    signal_received = 0;
    if (argc != 5)
    {
        fprintf(stderr, "ERROR: Invalid argument(s)\nUSAGE: hw3.out <listener-port> <seed> <dictionary-filename> <num-words>\n");
        return EXIT_FAILURE;
    }
    // validate inputs
    int port = atoi(*(argv + 1));
    if (port <= 0 || port > 65535)
    {
        fprintf(stderr, "ERROR: Invalid argument(s)\nUSAGE: hw3.out <listener-port> <seed> <dictionary-filename> <num-words>\n");
        return EXIT_FAILURE;
    }

    unsigned int seed = atoi(*(argv + 2));
    if (seed <= 0)
    {
        fprintf(stderr, "ERROR: Invalid argument(s)\nUSAGE: hw3.out <listener-port> <seed> <dictionary-filename> <num-words>\n");
        return EXIT_FAILURE;
    }

    char *filename = *(argv + 3);

    num_words = atoi(*(argv + 4));
    if (num_words <= 0)
    {
        fprintf(stderr, "ERROR: Invalid argument(s)\nUSAGE: hw3.out <listener-port> <seed> <dictionary-filename> <num-words>\n");
        return EXIT_FAILURE;
    }

    // read in dictionary
    int wordsfd = open(filename, O_RDONLY);
    if (wordsfd == -1)
    {
        fprintf(stderr, "ERROR: Invalid argument(s)\nUSAGE: hw3.out <listener-port> <seed> <dictionary-filename> <num-words>\n");
        return EXIT_FAILURE;
    }

    printf("MAIN: opened %s (%d words)\n", filename, num_words);
    words_list = calloc(num_words, sizeof(char *));
    initialize_dict(wordsfd, num_words);

    srand(seed);
    printf("MAIN: seeded pseudo-random number generator with %d\n", seed);

    // Ignore some signals
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGUSR1, signal_handler);

    if (pthread_mutex_init(&guess_lock, NULL) != 0 || pthread_mutex_init(&round_end_lock, NULL) != 0 || pthread_mutex_init(&words_lock, NULL) != 0)
    {
        perror("pthread_mutex_init() failed");
        return EXIT_FAILURE;
    }

    /** open up connection to the server */
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        perror("socket() failed");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    // allocate and bind socket
    // calloc(&server_addr, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    // let anyone connect

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in)) == -1)
    {
        perror("bind() failed");
        return EXIT_FAILURE;
    }

    printf("MAIN: Wordle server listening on port {%d}\n", port);
    if (listen(server_socket, MAX_CLIENTS) == -1)
    {
        perror("listen() failed");
        return EXIT_FAILURE;
    }

    hidden_words_counter = 0;
    // while true call accept; when accept unblocks and we have a connection, create a new thread.
    while (signal_received == 0)
    {
        struct sockaddr client;
        socklen_t client_len = sizeof(client);
        int client_socket = accept(server_socket, &client, &client_len);
        if (client_socket == -1)
        {
            break;
        }
        printf("MAIN: rcvd incoming connection request\n");
        pthread_t thread;
        pthread_create(&thread, NULL, handle_connection, (void *)&client_socket);
    }
    // printf("MAIN: MAIN: valid guesses: %d\n", total_guesses);
    // printf("MAIN: MAIN: win/loss: %d/%d\n", total_wins, total_losses);
    // int i = 1;
    // for(char **p = words; *p; ++p, ++i){
    //     printf("MAIN: MAIN: word #%d: %s\n", i, *p);
    // }
    
    // close(server_socket);
    return EXIT_SUCCESS;
}


/**
 * Play the wordle game with the client.
 */
void *handle_connection(void *fds)
{
    pthread_detach(pthread_self());
    int client_socket = *(int *)fds;
    int guesses_remaining = 6;

    char *hidden_word = *(words_list + (rand() % num_words));
    pthread_mutex_lock(&words_lock);
    ++hidden_words_counter;
    add_word(hidden_word);
    pthread_mutex_unlock(&words_lock);
    

    while (guesses_remaining != 0)
    {
        // char *recv_buffer = calloc(255, sizeof(char));
        char *recv_buffer = calloc(6, sizeof(char));
        char *send_buffer = calloc(9, sizeof(char));
        printf("THREAD %lu: waiting for guess\n", pthread_self());

        // receive guess from client
        // int n = read(client_socket, recv_buffer, 255);
        int n = recv(client_socket, recv_buffer, 5, MSG_WAITALL);
        // error checking goes here
        if (n == -1)
        {
            // fprintf(stderr, "ERROR: read() failed");
            free(recv_buffer);
            free(send_buffer);
            pthread_exit(NULL);
        }
        if (n == 0)
        {
            printf("THREAD %lu: client gave up; closing TCP connection...\n", pthread_self());
            pthread_mutex_lock(&round_end_lock);
            ++total_losses;
            pthread_mutex_unlock(&round_end_lock);
            printf("THREAD %lu: game over; word was %c%c%c%c%c!\n", pthread_self(), toupper(*(hidden_word)), toupper(*(hidden_word + 1)), toupper(*(hidden_word + 2)), toupper(*(hidden_word + 3)), toupper(*(hidden_word + 4)));
            free(recv_buffer);
            free(send_buffer);
            close(client_socket);
            pthread_exit(NULL);
        }
        char guess_won = 0;


        printf("THREAD %lu: rcvd guess: %s\n", pthread_self(),recv_buffer); //IF this is wrong, send 5 char bytes instead
        if (valid_guess(recv_buffer, send_buffer, hidden_word))
        {
            // assumes send_buffer now contains the guess result in the last 5 bytes
            *(send_buffer) = 'Y';
            --guesses_remaining;
            pthread_mutex_lock(&guess_lock);
            ++total_guesses;


            pthread_mutex_unlock(&guess_lock);

            if(guesses_remaining == 1){
            printf("THREAD %lu: sending reply: %s (%d guess left)\n", pthread_self(), send_buffer + 3, guesses_remaining);
            }
            else{
                printf("THREAD %lu: sending reply: %s (%d guesses left)\n", pthread_self(), send_buffer + 3, guesses_remaining);
            }
            if(strncmp(recv_buffer, hidden_word, 5) == 0){
                guess_won = 1;
            }
        }
        else
        {
            // send back ????? packet
            *send_buffer = 'N';
            for (int i = 3; i < 8; ++i)
            {
                *(send_buffer + i) = '?';
            }
            
            if(guesses_remaining == 1){
                printf("THREAD %lu: invalid guess; sending reply: %s (%d guess left)\n", pthread_self(), send_buffer + 3, guesses_remaining);
            }
            else{
                printf("THREAD %lu: invalid guess; sending reply: %s (%d guesses left)\n", pthread_self(), send_buffer + 3, guesses_remaining);
            }
            
        }
        // put remaining guesses in buffer and send
        // short g = (short)guesses_remaining;
        // *(send_buffer + 1) = (char)(g >> 8);
        // *(send_buffer + 2) = (char)(g);
        *(send_buffer + 2) = (char)guesses_remaining;

        
        // somewhere here we should determine if the player just won or not
        
        write(client_socket, send_buffer, 8);

        if (guess_won == 1)
        {
            printf("THREAD %lu: game over; word was %c%c%c%c%c!\n", pthread_self(), toupper(*(hidden_word)), toupper(*(hidden_word + 1)), toupper(*(hidden_word + 2)), toupper(*(hidden_word + 3)), toupper(*(hidden_word + 4)));
            pthread_mutex_lock(&round_end_lock);
            ++total_wins;
            pthread_mutex_unlock(&round_end_lock);
            free(recv_buffer);
            free(send_buffer);
            return NULL;
        }
        free(recv_buffer);
        free(send_buffer);
    }
    // round lost
    printf("THREAD %lu: game over; word was %c%c%c%c%c!\n", pthread_self(), toupper(*(hidden_word)), toupper(*(hidden_word + 1)), toupper(*(hidden_word + 2)), toupper(*(hidden_word + 3)), toupper(*(hidden_word + 4)));
    pthread_mutex_lock(&round_end_lock);
    ++total_losses;
    pthread_mutex_unlock(&round_end_lock);
    close(client_socket);
    return NULL;
}


/**
 * Check if word is a valid guess by seeing if it is in words_list. Returns 1 for a valid guess, 0 for an invalid guess.
 * Should also make this function edit buffer to store the guess in the last 5 bytes of buffer.
 *
 */
int valid_guess(char *word, char *buffer, char *actual)
{
    for(int i = 0; i < 5; ++i){
        *(word + i) = tolower(*(word + i));
    }
    // If actual letter checked, +2, if word letter checked, +1
    int *checked = (int *)calloc(5, sizeof(int));
    for (int i = 0; i < num_words; ++i)
    {
        if (strcmp(word, *(words_list + i)) == 0)
        {
            for (int j = 3; j < 8; ++j)
            {
                *(buffer + j) = '-';
            }
            // Word is valid, now populate buffer with the result
            // Unless I add a parameter that passes hiddenword, I will not know it
            for (int j = 0; j < 5; ++j)
            {
                if (*(word + j) == *(actual + j))
                {
                    *(buffer + j + 3) = toupper(*(word + j));
                    *(checked + j) = 3;
                }
            }
            // j is index for guess
            for (int j = 0; j < 5; ++j)
            {
                // Stops if checked is 1 or 3(used in guess or matched in both)
                if (*(checked + j) % 2 != 1)
                {
                    // k is index for actual word
                    for (int k = 0; k < 5; ++k)
                    {
                        if (*(checked + k) < 2 && *(word + j) == *(actual + k))
                        {
                            *(buffer + j + 3) = *(word + j);
                            *(checked + k) += 2;
                            *(checked + j) += 1;
                            break;
                        }
                    }
                }
            }
            free(checked);
            return 1;
        }
    }
    free(checked);
    return 0;
}