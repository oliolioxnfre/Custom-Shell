#include <stdio.h>
#include <string.h>

int main() {
    char name[100];
    printf("Enter your name (or 'exit' to exit and die to 'fail')\n");
    
     
    while (fgets(name, sizeof(name), stdin) != NULL) {
        if(strcmp(name, "exit\n") == 0) return 0;
        if(strcmp(name, "die\n") == 0) return 1;
        
        name[strcspn(name, "\n")] = 0; // Remove trailing newline
        printf("Hello, %s!\n", name);
    }
    return 0;
}