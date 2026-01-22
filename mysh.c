#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define MAX_LINE 1026
#define PROMPT "mysh> "

// 0 if previous command was executed
// 1 if previous command was not executed
int previousStatus = 1;

char *my_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *new_str = malloc(len);
    if (new_str) {
        memcpy(new_str, s, len);
    }
    return new_str;
}

//     -1 = dont execute, 0 = execute no conditionals, 1 = execute given conditional
int conditional(char *startingToken) {
    if (strcmp(startingToken, "or") == 0) {
        if (previousStatus == 1) return 1;
        else return -1;
    } else if (strcmp(startingToken, "and") == 0) {
        if (previousStatus == 0) return 1;
       
        else return -1;
    }


    return 0;
}


// Checks if the name matches the * pattern.
int wildcardMatch(const char *name, const char *pattern) {
    const char *asterisk = strchr(pattern, '*');
    if (!asterisk) {
        return strcmp(name, pattern) == 0;
    }

    int beforeLen = (int)(asterisk - pattern);
    int afterLen = strlen(pattern) - beforeLen - 1;
  
    // Check prefix
    if (strncmp(name, pattern, beforeLen) != 0) {
        return 0; // Prefix doesn't match
    }  
    if (afterLen > 0) {

        if (strcmp(name + strlen(name) - afterLen, asterisk + 1) != 0) {
            return 0; 

        }
    }
    return 1; 
}


void expandWildcard(const char *token, char ***tokens, int *token_count) {
    if (!strchr(token, '*')) {
        (*tokens)[(*token_count)++] = my_strdup(token);
        return;
    }


    const char *lastSlash = strrchr(token, '/');
    char dirPath[MAX_LINE] = ".";
    const char *pattern = token;
    if (lastSlash) {
        size_t length = lastSlash - token;
        strncpy(dirPath, token, length);
        dirPath[length] = '\0';
        pattern = lastSlash + 1;
    } 

    // Open dir
    DIR *dir = opendir(dirPath);
    if (!dir) { 
        // If we can't open dir, just store the original token
        (*tokens)[(*token_count)++] = my_strdup(token);
        return;     
    }   

    struct dirent *entry;
    int matchFound = 0;
    while ((entry = readdir(dir)) != NULL) {
        // Ignore "." and ".."
        if (entry->d_name[0] == '.' && pattern[0] != '.')
            continue;

        if (wildcardMatch(entry->d_name, pattern)) {
            matchFound = 1;
            // Build full path if dirPath != "."
            if (strcmp(dirPath, ".") == 0) {
                (*tokens)[(*token_count)++] = my_strdup(entry->d_name);
            } else {
                char fullPath[MAX_LINE];
                int pathlen = snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);
                if (pathlen < 0 || pathlen >= sizeof(fullPath)) {
                    fprintf(stderr, "Path too long, skipping: %s/%s\n", dirPath, entry->d_name);
                    continue;
                }
                (*tokens)[(*token_count)++] = my_strdup(fullPath);
            }
        }
    }
    closedir(dir);

    if (!matchFound) {
        // No match; keep original token
        (*tokens)[(*token_count)++] = my_strdup(token);
    }
}


//Tokenizer
char **tokenize(char *str) {
    int token_len = 0;
    
    char token[MAX_LINE];
    int token_count = 0;
    char **tokens = malloc(MAX_LINE * sizeof(char *));


    for(int i =0; str[i] != '\0'; i++){
        char c = str[i];

        if (c == ' ' || c == '\t') {
            if (token_len > 0) {
                token[token_len] = '\0';
                expandWildcard(token, &tokens, &token_count);
                token_len = 0;
            }
        } else if (c == '<' || c == '>' || c == '|') {
            if (token_len > 0) {
                token[token_len] = '\0';
                expandWildcard(token, &tokens, &token_count);
                token_len = 0;
            }
            // treat special character as token
            char special[2]; 
            special[0] = c;
            special[1] = '\0';
            tokens[token_count++] = my_strdup(special);
        } else if (c == '#') {
            // comment: ignore the rest
            break;
        } else {
            token[token_len++] = c;
        }
    }

    // trailing token
    if (token_len > 0) {
        token[token_len] = '\0';
        expandWildcard(token, &tokens, &token_count);
    }

    tokens[token_count] = NULL;
    return tokens;
}


// used for which and cd
//  Returns 1 if found, 0 if not found
char *searchExecutable(const char *command) {
    static char pathBuffer[MAX_LINE];

    if (strchr(command, '/')) {
        // Path already specified
        return (char *)command;
    }

    // Include the current directory in the search paths
    const char *searchPaths[] = {".", "/usr/local/bin", "/usr/bin", "/bin", NULL};
    for (int i = 0; searchPaths[i]; i++) {
        snprintf(pathBuffer, sizeof(pathBuffer), "%s/%s", searchPaths[i], command);
        if (access(pathBuffer, X_OK) == 0) {
            return pathBuffer;
        }
    }
    return NULL; // Not found
}


//0 = found and handled     1 = not found   2 = found and error
int executeBuiltIn(char **tokens) {
    if (!tokens[0]) return 0; //if no tokens return 1

    if (strcmp(tokens[0], "cd") == 0) {
        if (!tokens[1] || tokens[2]) {
            fprintf(stderr, "cd: Invalid arguments\n");
            previousStatus = 1; // fail
            return 2;
        } else if (chdir(tokens[1]) != 0) { 
            perror("cd");
            previousStatus = 1; // fail
            return 2;
        } else {
            previousStatus = 0; // success
            return 0;
        }
    } else if (strcmp(tokens[0], "pwd") == 0) {
        char cwd[MAX_LINE];
        if (getcwd(cwd, sizeof(cwd))) {
            printf("%s\n", cwd);
            previousStatus = 0; // success
            return 0;
        } else {
            perror("pwd");
            previousStatus = 1; // fail
            return 2;
        }
    } else if (strcmp(tokens[0], "which") == 0) {
        if (!tokens[1] || tokens[2]) {
            previousStatus = 1; // fail 2 many args
            return 2;
        }
        //if which = built ins print nothing
        else if (strcmp(tokens[1], "which") == 0 || strcmp(tokens[1], "cd") == 0 || strcmp(tokens[1], "pwd") == 0)
        {
            previousStatus = 1; // fail
            return 2;
        }

        char *path = searchExecutable(tokens[1]);

        if (!path) {
            previousStatus = 1; // fail
            return 2;
        } else if (strchr(tokens[1], '/')) {
            printf("%s\n", tokens[1]); // user gave a path
            previousStatus = 0;       // success
            return 0;
        } else {
            printf("%s\n", path);  // found in known paths
            previousStatus = 0;       // success
            return 0;
        }
    } else if (strcmp(tokens[0], "exit") == 0) {
        printf("Exiting my shell.\n");
        exit(EXIT_SUCCESS);
    } else if (strcmp(tokens[0], "die") == 0) {
        for (int i = 1; tokens[i]; i++) {
            printf("%s ", tokens[i]);
        }
        printf("\n");
        exit(EXIT_FAILURE);
    }
    return 1; // not a built-in
}


void redirection(char **tokens) {
    for (int i = 0; tokens[i]; i++) {
        if (strcmp(tokens[i], "<") == 0) {
            
            if (strcmp(tokens[i + 1], "<") == 0 || strcmp(tokens[i + 1], ">") ==0) {
                fprintf(stderr, "Syntax Error\n");
                previousStatus = 1;
                exit(EXIT_FAILURE);
            }
            
            // Handle input redirection
            int fd = open(tokens[i + 1], O_RDONLY);
            if (fd < 0) {
                perror("open");
                exit(EXIT_FAILURE);
            }
            if (0 > dup2(fd, 0)) {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(fd);
        } else if (strcmp(tokens[i], ">") == 0) {
            
            if (strcmp(tokens[i + 1], "<") == 0 || strcmp(tokens[i + 1], ">" )== 0) {
                fprintf(stderr, "Syntax Error\n");
                previousStatus = 1;
                exit(EXIT_FAILURE);
            }
            int fd = open(tokens[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("open");
                exit(EXIT_FAILURE);
            }
            if (dup2(fd, STDOUT_FILENO) < 0) {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            
            close(fd);
        }   }}       


int executeCommand(char **tokens) {    // If built-in, execute directly
    
    int x = executeBuiltIn(tokens);
    if (x != 1) {
        if (x == 0) x = 0;
        else x = 1;
        return x;
    }
    

    char *commandPath = searchExecutable(tokens[0]);   // find path for external command

    
    if (!commandPath) {//if command not found
        fprintf(stderr, "Command not found: %s\n", tokens[0]);
        previousStatus = 1; // fail         
        //printf("exit status: %d\n", previousStatus);
        
        return previousStatus;
    }

    // fork and exec
    pid_t pid = fork(); 
    if (pid < 0) {              
        perror("fork"); 
        previousStatus = 1;
        //printf("fork exit status: %d\n", previousStatus);
        return 1; //return 1 if 
    }


    if (pid == 0) { 
        redirection(tokens);
        // remove redirection tokens from the argument list
        char *args[MAX_LINE];
        int x = 0;
        for (int i = 0; tokens[i]; i++) {
            if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0) {
                i++; // skip next token
                continue;
            }
            args[x++] = tokens[i];
        }
        args[x] = NULL;

        execv(commandPath, args);
        perror("execv");


        exit(EXIT_FAILURE);
    } else { 
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            previousStatus = 0; // command found executed properly
        } else {
            previousStatus = 1; // command found executed but failed
        }
    }
    //printf("final status: %d\n", previousStatus);
    return previousStatus;
}



void executeMultiPipeline(char ***commands, int cmdCount) {
    int pipes[MAX_LINE][2];
    pid_t pids[MAX_LINE];
    
    for (int i = 0; i < cmdCount-1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");

            previousStatus = 1;
            return;
        }
    }
    
    for (int i = 0; i < cmdCount; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            previousStatus = 1;
            return;
        }
        
        if (pids[i] == 0) {
            if (i > 0) {int x = 1;
                dup2(pipes[i - x][0], 0);
            }

            int zcount = cmdCount - 1;
            if (i < zcount) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            for (int j = 0; j < zcount; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            if (executeBuiltIn(commands[i]) != 1) exit(previousStatus);
        
            if (i ==0 || i== cmdCount - 1) redirection(commands[i]);

            char *args[MAX_LINE];
            int x = 0;
            if (i == 0 || i == cmdCount-1) {
                for (int j = 0; commands[i][j]; j++) {
                    if (strcmp(commands[i][j], "<") == 0 || strcmp(commands[i][j], ">") == 0) {
                        j++; // skip next token
                        continue;
                    }
                    args[x++] = commands[i][j];
                }
                args[x] = NULL;
            } else {
                // For middle commands, use commands as-is
                for (int j = 0; commands[i][j]; j++) {
                    args[x++] = commands[i][j];
                }
                args[x] = NULL;
            }
            
            char *cmdPath = searchExecutable(args[0]);
            if (!cmdPath) {
                fprintf(stderr, "Command not found: %s\n", args[0]);
                exit(1);
            }
            
            execv(cmdPath, args);
            perror("execv");
            exit(EXIT_FAILURE);
        }
    }
    
    // Parent: close all pipe ends
    for (int i = 0; i < cmdCount-1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all children
    int lastStatus;
    for (int i = 0; i < cmdCount; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == cmdCount-1) {
            lastStatus = status;
        }
    }
    
    // Final status depends on the last command
    if (WIFEXITED(lastStatus) && WEXITSTATUS(lastStatus) == 0) {
        previousStatus = 0; // success
    } else {
        previousStatus = 1; // fail
    }
}

void parseAndRunCommand(char **tokens) {
    if (!tokens[0]) return; // empty

    int result = conditional(tokens[0]);
    int start = 0; // index of actual command
    if (result == -1) {
        // skip command
        return;
    } else if (result == 1) {
        start = 1; 
    }

    // If tokens[start] is NULL, there's nothing to run
    if (!tokens[start]) return;

    // Count pipes and find pipe indices
    int pipeCount = 0;
    int pipeIndices[MAX_LINE];
    
    for (int i = start; tokens[i]; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            pipeIndices[pipeCount++] = i;
        }
    }

    if (pipeCount > 0) {
        // Prepare commands array
        char **commands[MAX_LINE];
        
        commands[0] = &tokens[start];
        
        for (int i = 0; i < pipeCount; i++) {
            tokens[pipeIndices[i]] = NULL;
            commands[i + 1] = &tokens[pipeIndices[i] + 1];
            
            if (!commands[i + 1][0]) {
                fprintf(stderr, "Syntax error: missing command after pipe\n");
                previousStatus = 1;
                return;
            }
        }
        executeMultiPipeline(commands, pipeCount + 1);
    } else {
        executeCommand(&tokens[start]);
    }
}

// ---------------------------------------------------------------------------
// main() - sets up interactive or batch mode, reads lines with read(),
// tokenizes, and executes commands until exit or EOF.
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    int fd = 0;
    char buffer[MAX_LINE];
    ssize_t bytes_read;
    ssize_t buf_used = 0;
    int argmax = 2;

    if (argc > argmax) { //too many args
        fprintf(stderr, "Usage: %s [batch_file]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Check if batch mode with file
    if (argc == 2) {
        fd = open(argv[1], O_RDONLY);
       // batchArg = 1;
        if (fd < 0) {
            perror("Error opening batch file");
            exit(EXIT_FAILURE);
        }
    }

    // Interactive check
    int interactive = isatty(fd);
    if (interactive) {
        printf("Welcome to my shell!\n");
    }

    // Main loop
    while (1) {
        // Print prompt if interactive
        if (interactive) {
            write(STDOUT_FILENO, PROMPT, strlen(PROMPT));
        }


        // Reset buffer, read one line
        buf_used = 0;
        while ((bytes_read = read(fd, &buffer[buf_used], 1)) > 0) {
            if (buffer[buf_used] == '\n') {
                buffer[buf_used] = '\0';
                break;
            }
            buf_used++;
            if (buf_used >= MAX_LINE - 1) {
                buffer[buf_used] = '\0';
                break;
            }
        }

        if (bytes_read <0) {
            perror("read error");
            break;
        }
        if (bytes_read == 0) {
            // Process any remaining command in buffer before exiting
            if (buf_used > 0) {
                buffer[buf_used] = '\0';
                char **tokens = tokenize(buffer);
                parseAndRunCommand(tokens);
                for (int i = 0; tokens[i]; i++) free(tokens[i]);
                free(tokens); }
            break;
        }

        // Print the command if in batch mode
       // if (batchArg == 1) {
       // printf("%s\n", buffer);}
        // Tokenize
        char **tokens = tokenize(buffer);
        if (tokens[0] && (strcmp(tokens[0], "exit") == 0 || strcmp(tokens[0], "die") == 0)) {
            parseAndRunCommand(tokens);
            free(tokens);
            break;
        }

        parseAndRunCommand(tokens);

        for (int i = 0; tokens[i]; i++) {
            free(tokens[i]);
        }
        free(tokens);
    }

    if (interactive) {
        printf("Exiting my shell.\n");
    }

    if (argc == 2) {
        close(fd);
    }

    return EXIT_SUCCESS;
}