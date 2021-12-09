#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <dirent.h> 
#include <signal.h>
#include <fcntl.h>
 
#define MAX_LINE 80 /* 80 chars per line, per command, should be enough. */

#define TO_FILE_CREATE_FLAGS (O_WRONLY | O_CREAT | O_TRUNC)
#define APPEND_CREATE_FLAGS (O_WRONLY | O_CREAT | O_APPEND)
#define CREATE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

// will be used for alias/unalias functionality
struct aliasNode {
    char* aliasName;
    char* command;
    struct aliasNode* next;
};
struct aliasNode *head = NULL;

// It is used for redirection types
// TO_FILE = >
// APPEND = >>
// FROM_FILE = <
// TO_STDOUT is default io device (terminal)
enum redirection_type {TO_FILE, APPEND, FROM_FILE, TO_STDOUT};


int status = 0;
pid_t forkResult; // holds the result of fork()
// function headers
void findExecutablePath(char * args[MAX_LINE/2 + 1], char executablePath[MAX_LINE]);
void printAliases();
void addAlias(char* aliasName, char* command);
void removeAlias(char* aliasName);
struct aliasNode* findAlias(char* aliasName);
void alias(char * args[MAX_LINE/2 + 1]);
void handlerFunction();
enum redirection_type getIoRedirectionType(char * args[MAX_LINE/2 + 1]);
void changeIoDevice(enum redirection_type io_device, char * args[MAX_LINE/2 + 1]);
void clearRedirectionTypeFromArgs(char * args[MAX_LINE/2 + 1]);
int findRedirectionFilenameIndex(char * args[MAX_LINE/2 + 1]);
void switchIoToFile(char* filename);
void switchIoToAppend(char* filename);

/* The setup function below will not return any value, but it will just: read
in the next command line; separate it into distinct arguments (using blanks as
delimiters), and set the args array entries to point to the beginning of what
will become null-terminated, C-style strings. */


void setup(char inputBuffer[], char *args[],int *background)
{
    int length, /* # of characters in the command line */
        i,      /* loop index for accessing inputBuffer array */
        start,  /* index where beginning of next command parameter is */
        ct;     /* index of where to place the next parameter into args[] */
    
    ct = 0;
        
    /* read what the user enters on the command line */
    length = read(STDIN_FILENO,inputBuffer,MAX_LINE);  

    /* 0 is the system predefined file descriptor for stdin (standard input),
       which is the user's screen in this case. inputBuffer by itself is the
       same as &inputBuffer[0], i.e. the starting address of where to store
       the command that is read, and length holds the number of characters
       read in. inputBuffer is not a null terminated C-string. */

    start = -1;
    if (length == 0)
        exit(0);            /* ^d was entered, end of user command stream */

/* the signal interrupted the read system call */
/* if the process is in the read() system call, read returns -1
  However, if this occurs, errno is set to EINTR. We can check this  value
  and disregard the -1 value */
    if ( (length < 0) && (errno != EINTR) ) {
        perror("error reading the command");
	exit(-1);           /* terminate with error code of -1 */
    }
    
	// printf(">>%s<<",inputBuffer);
    for (i=0;i<length;i++){ /* examine every character in the inputBuffer */

        switch (inputBuffer[i]){
	    case ' ':
	    case '\t' :               /* argument separators */
		if(start != -1){
                    args[ct] = &inputBuffer[start];    /* set up pointer */
		    ct++;
		}
                inputBuffer[i] = '\0'; /* add a null char; make a C string */
		start = -1;
		break;

            case '\n':                 /* should be the final char examined */
		if (start != -1){
                    args[ct] = &inputBuffer[start];     
		    ct++;
		}
                inputBuffer[i] = '\0';
                args[ct] = NULL; /* no more arguments to this command */
		break;

	    default :             /* some other character */
		if (start == -1)
		    start = i;
                if (inputBuffer[i] == '&'){
		    *background  = 1;
                    inputBuffer[i-1] = '\0';
		}
	} /* end of switch */
     }    /* end of for */
     args[ct] = NULL; /* just in case the input line was > 80 */

	//for (i = 0; i <= ct; i++)
	//	printf("args %d = %s\n",i,args[i]);
} /* end of setup routine */
 
int main(void)
{
            char inputBuffer[MAX_LINE]; /*buffer to hold command entered */
            int background; /* equals 1 if a command is followed by '&' */
            char *args[MAX_LINE/2 + 1]; /*command line arguments */

            
            char executablePath[MAX_LINE]; // holds the executable path that user wants to run

            // setting up signal handler
            struct sigaction act;
            act.sa_handler = handlerFunction;            /* set up signal handler */
            act.sa_flags = 0;
            if ((sigemptyset(&act.sa_mask) == -1) || (sigaction(SIGTSTP, &act, NULL) == -1)) {
                perror("Failed to set SIGTSTP handler");
                return 1;
            }

            enum redirection_type currentRedirectionType;

            
            while (1){
                        strcpy(executablePath, "");
                        background = 0;
                        printf("myshell: \n");
                        /*setup() calls exit() when Control-D is entered */
                        setup(inputBuffer, args, &background);


                        currentRedirectionType = getIoRedirectionType(args);



                        // if alias command is called
                        if (args[0] != NULL && strcmp(args[0], "alias")==0) {
                            // if user wants a list of set aliases
                            if (args[1] != NULL && strcmp(args[1], "-l")==0) {
                                printAliases();
                            }
                            else {
                              alias(args);
                            }

                            
                            continue;
                        }

                        // if unalias command is called
                        if (args[0] != NULL && strcmp(args[0], "unalias")==0) {
                            removeAlias(args[1]);
                            continue;
                        }

                        struct aliasNode* alias_node = findAlias(args[0]);
                        // if alias found
                        if (alias_node != NULL) {
                            system(alias_node->command);
                            continue;
                        }

                        if (strcmp(args[0], "exit")==0) {
                            if (!background) {
                                exit(0);
                            }
                        }
                        
                        forkResult = fork();
                        if (forkResult < 0) { 
                            perror("Failed to fork"); 
                            return 1; 
                        }
                        // child process
                        else if (forkResult == 0)  {
                            // Find the desired executable's directory
                            // by searching each directory in $PATH
                            
                            findExecutablePath(args, executablePath);
                            // if executable does not exist
                            if (strcmp(executablePath, "")==0 ) {
                                fprintf(stderr, "%s executable could not found !!\n", args[0]);
                            }
                            else { // execution
                                printf("%s executable path is: %s \n", args[0], executablePath);
                                // if it is a background process
                                // remove ampersand from args
                                if (background) {
                                    args[1] = NULL;
                                }
                                
                                changeIoDevice(currentRedirectionType, args);
                                clearRedirectionTypeFromArgs(args);

                                execv(executablePath, args);
                            }
                            
                            
                        }

                        // parent process
                        else if (forkResult > 0) {
                            // if it is not a background process
                            // wait for it
                            if (background) {
                                printf("!!background process, not waiting for it \n");
                            }
                                
                            else {
                                // wait for all childs
                                while (wait(&status) > 0);
                            }
                                
                                
                        }
                       
            }
}

// Finds executable path from $PATH environment variable
void findExecutablePath(char * args[MAX_LINE/2 + 1], char executablePath[MAX_LINE]) {
    // printf("findExecutablePath is running\n");

    // gets executable name from command line args
    char executableName[MAX_LINE/2 + 1];
    strcpy(executableName, args[0]);
    
    char *directories;
    char *directory;
    char foundExecutableDirectory[MAX_LINE];
    directories = strdup(getenv("PATH")); // gets everything in $PATH with delimitor ':'
    DIR *dir_current;
    struct dirent *dir_entry;
    
    // slices every directory and loops through them
    while( (directory=strsep(&directories,":")) != NULL ) {
        // Loops every filename in directory variable
        // if filename is same as executableName
        // copy path to executablePath and return
        // printf("files under %s\n",directory);
        dir_current = opendir(directory);
        
        while ((dir_entry = readdir(dir_current)) != NULL) {
            // avoid "." and ".."
            if (strcmp(dir_entry->d_name, ".") != 0 && strcmp(dir_entry->d_name, "..") != 0) {
                // printf("\t%s \n", dir_entry->d_name);
                // if filename matches executableName
                if (strcmp(dir_entry->d_name, executableName)==0) {
                    // printf("there is a match!! \n");
                    // printf("%s \n", directory);
                    // printf("%s \n", dir_entry->d_name);
                    strcpy(foundExecutableDirectory, directory);
                    strcat(foundExecutableDirectory, "/");
                    strcat(foundExecutableDirectory, executableName);

                    strcpy(executablePath, foundExecutableDirectory);
                    return;
                }

            }
                
        }    
    }
}


void printAliases() {
    struct aliasNode* current = head;
    while (current != NULL) {
        printf("\t %s \"%s\"\n", current->aliasName, current->command);
        current = current->next;
    }
}

// creates new alias node
// and insert to linked list
void addAlias(char* aliasName, char* command) {
    struct aliasNode* node = (struct aliasNode*)malloc(sizeof(struct aliasNode));
    node->aliasName = strdup(aliasName);
    node->command = strdup(command);
    strcpy(node->aliasName, aliasName);
    strcpy(node->command, command);

    node->next=head;
    head=node;
}

// removes alias node
// with given alias name
void removeAlias(char* aliasName) {
    struct aliasNode* current = head;
    struct aliasNode* previous = NULL;

    if(head == NULL)
        return;
    

    while(strcmp(current->aliasName, aliasName)!=0) {
        if (current->next == NULL)
            return;
        else {
            previous = current;
            current = current->next;
        }
    }

    if (current == head) {
        head = head->next;
    }
    else {
        previous->next = current->next;
    }
}


void alias(char * args[MAX_LINE/2 + 1]) {
    // for getting the last element in args
    // which is alias name
    int aliasNameIndex;
    for (aliasNameIndex=0; aliasNameIndex<MAX_LINE/2 + 1; aliasNameIndex++) 
        if (args[aliasNameIndex] == NULL) break;
    aliasNameIndex--;

    char aliasName[MAX_LINE/2 + 1];
    strcpy(aliasName, args[aliasNameIndex]);


    
    // print commands
    char commandWithQuotes[MAX_LINE/2 + 1];
    strcpy(commandWithQuotes, "");
    for (int i = 1; i < aliasNameIndex; i++) {
        strcat(commandWithQuotes, args[i]);
        strcat(commandWithQuotes, " ");
    }



    char command[MAX_LINE/2 +1];
    strncpy(command, commandWithQuotes+1, strlen(commandWithQuotes)-3);
    command[strlen(command)] = '\0';


    addAlias(aliasName,command);
}


struct aliasNode* findAlias(char* aliasName) {
    struct aliasNode* current = head;

    if (head == NULL) {
        return NULL;
    }

    while (strcmp(current->aliasName, aliasName)!=0) {
        if (current->next == NULL) {
            return NULL;
        }
        else {
            current = current->next;
        }
    }
    return current;
}

void handlerFunction(int signo) {
    printf("\nhandler function is called \n");
    printf("exiting shell.. \n");
    exit(0);
}

// detects the IO type from args
enum redirection_type getIoRedirectionType(char * args[MAX_LINE/2 + 1]) {
    int i;
    // loops arguments
    for (i=1; args[i] != NULL; i++) {

        // if TO_FILE
        if (strcmp(args[i], ">")==0) {
            return TO_FILE;
        }

        // if APPEND
        if (strcmp(args[i], ">>")==0) {
            return APPEND;
        }

        // If FROM_FILE (<)
        if (strcmp(args[i], "<")==0) {
            return FROM_FILE;
        }
    }

    // default io device (the terminal)
    return TO_STDOUT;
}


void changeIoDevice(enum redirection_type io_device, char * args[MAX_LINE/2 + 1]) {

    if (io_device == TO_STDOUT) {
        printf("redirection type is TO_STDOUT\n");
        return;
    }

    int fd;
    int fileNameIndex = findRedirectionFilenameIndex(args);
    if (io_device == TO_FILE) {
        printf("redirection type is TO_FILE\n");
        switchIoToFile(args[fileNameIndex]);
    }
    else if (io_device == APPEND) {
        printf("redirection type is APPEND\n");
        switchIoToAppend(args[fileNameIndex]);
    }
    else if (io_device == FROM_FILE) {        
        printf("redirection type is FROM_FILE\n");
    
    
    }
    clearRedirectionTypeFromArgs(args);


}

int findRedirectionFilenameIndex(char * args[MAX_LINE/2 + 1]) {
    int i;
    for (i=1; args[i] != NULL; i++) {
        // if either ">",  ">>" or "<"
        if ((strcmp(args[i], ">")==0) || (strcmp(args[i], ">>")==0) || (strcmp(args[i], "<")==0)) {
            return i + 1;
        }
    }

}

void clearRedirectionTypeFromArgs(char * args[MAX_LINE/2 + 1]) {
    int i;
    for (i=1; args[i] != NULL; i++) {
        // if either ">",  ">>" or "<"
        if ((strcmp(args[i], ">")==0) || (strcmp(args[i], ">>")==0) || (strcmp(args[i], "<")==0)) {
            args[i] = NULL;
            return;
        }
    }
}

void switchIoToFile(char* filename) {
    int fd = open(filename, TO_FILE_CREATE_FLAGS, CREATE_MODE);
    if (fd == -1) {
        fprintf(stderr, "Failed to open file \n");
        exit(1);
    }
    if (dup2(fd, STDOUT_FILENO) == -1) {
        fprintf(stderr, "Failed to redirect standard output\n");
        exit(1);
    }
    if (close(fd) == -1) {
        fprintf(stderr, "Failed to close the file\n");
        exit(1);
    }
}

void switchIoToAppend(char* filename) {
    int fd = open(filename, APPEND_CREATE_FLAGS, CREATE_MODE);
    if (fd == -1) {
        fprintf(stderr, "Failed to open file \n");
        exit(1);
    }
    if (dup2(fd, STDOUT_FILENO) == -1) {
        fprintf(stderr, "Failed to redirect standard output\n");
        exit(1);
    }
    if (close(fd) == -1) {
        fprintf(stderr, "Failed to close the file\n");
        exit(1);
    }
}