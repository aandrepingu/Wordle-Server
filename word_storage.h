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

extern char **words;
extern int hidden_words_counter;

// stores all words in the game
extern char **words_list;
extern int num_words;
/**
 * Reads in num_words words from the file descriptor and stores them in words_list. Assumes valid dictionary file, but may need to add
 * error checking.
 */
void initialize_dict(int wordsfd, int num_words)
{

    char *buffer = calloc(6, sizeof(char));
    char **next = words_list;
    int bytes_read;
    for (int i = 0; i < num_words; ++i, ++next)
    {
        bytes_read = read(wordsfd, buffer, 6);
        if (bytes_read != 6 * sizeof(char) || *(buffer + 5) != '\n')
        {
            free(buffer);
            close(wordsfd);
            return;
        }
        // add word to the table
        char *word = calloc(6, sizeof(char));
        // How are we meant to free word unless we free all of the words_list at the end?
        // I don't think freeing words_list would even work since word is stored in next, not words_list
        for (int j = 0; j < 5; ++j)
        {
            // non-word
            // if(!isalpha(*(buffer + j))){
            //     free(word);
            //     free(buffer);
            //     close(rc);
            //     return -1;
            // }
            *(word + j) = tolower(*(buffer + j));
        }
        *next = word;
    }
    free(buffer);
    close(wordsfd);
    return;
}


/**
 * adds a new hidden word to the end of the words array.
 */
void add_word(char *hidden_word){
    words = realloc(words, sizeof(char*) * (hidden_words_counter + 1));
    *(words + hidden_words_counter - 1) = calloc(6, sizeof(char));
    for(int i = 0; i < 5; ++i){
        *(*(words+hidden_words_counter-1) + i) = toupper(*(hidden_word + i));
    }
    *(words + hidden_words_counter) = NULL;
}
