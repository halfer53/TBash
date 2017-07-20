#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <errno.h>
#include <termios.h>
#include "parse.h"
#include "tsh.h"
#include <poll.h>


#define READ_END 0
#define WRITE_END 1
#define ENV_CHA_LEN 1024
#define ENV_LEN 16
#define LIMIT 10240
#define errExit(a) exit(-1);

#define move_up()     printf("\033[1A"); // Move up X lines;
#define move_down()   printf("\033[1B"); // Move down X lines;
#define move_right()  printf("\033[1C"); // Move right X column;
#define move_left()   printf("\033[1D"); // Move left X column;



//search the name in the PATH environment, and try to locate the location of that command name
//on success, buffer is filled with the path of the file, on failure, 1 is returned;
int search_path(char *buffer, char *name){
    char *envpath = getenv("PATH");
    char *path = malloc(sizeof(char) * strlen(envpath) + 1);
    char *path_bak = path;
    const char path_spliter[2] = ":";
    char *token;
    int ret = 1;

    strcpy(path,envpath);
    token = strtok_r(path,path_spliter,&path);

    while(token != NULL){
        sprintf(buffer,"%s%c%s",token,'/',name);
        if(access(buffer,R_OK) == 0){
            ret = 0;
            break;
        }
        token = strtok_r(path,path_spliter,&path);
    }
    free(path_bak);
    return ret;
}

//execute command, given the commands stored in line
int exec_cmd(char *line, int tpipe[2]){
    char *buffer = malloc(sizeof(char) * BUFFER_MAX);
    int i;
    struct commandLine cmd;
    int status = 0;
    char *cmd_name;
    int sin = STDIN_FILENO, sout = STDOUT_FILENO;
    int fd_in = 0;
    int pipefd[2];
    char **tmp_args;
    int saved_stdin, saved_stdout;

    if(strcmp("exit", line) == 0)
        exit(0);

    if(*line == '#') //if this is a comment
        return 0;

    
    if(Parse(line,&cmd) != 0){ //parsing
        fprintf(stderr, "Parsing failed\n");
        exit(0);
    }
    if(line[0] == '.' && line[1] == '/'){
        //printf("%s %s",cmd.argv[cmd.cmdStart[i]]+2,cmd.argv + cmd.cmdStart[i]);
        if(!fork()){
            if(cmd.background){
                // printf("back");
                setpgid(0,0);
            }
            execv(cmd.argv[cmd.cmdStart[i]]+2,cmd.argv + cmd.cmdStart[i]);
        }
        wait(NULL);
        return 0;
    }

    if(tpipe != NULL){
        dup2(tpipe[WRITE_END],STDOUT_FILENO);
        close(tpipe[WRITE_END]);
    }

    if(cmd.infile){ //if redirecting input
        saved_stdin = dup(STDIN_FILENO); //backup stdin
        sin = open(cmd.infile,O_RDONLY);
        dup2(sin,STDIN_FILENO);
        close(sin);
    }
    
    if(cmd.outfile){ //if redirecting output
        saved_stdout = dup(STDOUT_FILENO); //backup stdout
        int mode = O_WRONLY | O_CREAT;
        if(cmd.append) //if append
            mode |= O_APPEND;
        else //else replace theoriginal document
            mode |= O_TRUNC;
        sout = open(cmd.outfile, mode, 0644);
        dup2(sout,STDOUT_FILENO);
        close(sout);
    }

//    if(fd != 0){
//        dup2(fd,STDOUT_FILENO);
//        close(fd);
//    }

    if(cmd.env && cmd.env_val){ //if a new environment variable is set
//        printf("env %s %s\n",cmd.env,cmd.env_val);
        if(setenv(cmd.env,cmd.env_val,1)){
            perror("Env failed: ");
            exit(0);
        }
        return 0;
    }
    
    
    for( i=0; i< cmd.numCommands; i++){ //go through list of commands in a single line
        
        cmd_name = cmd.argv[cmd.cmdStart[i]];
        //cd 
        if(strcmp(cmd_name,"cd") == 0){
            if(chdir(cmd.argv[cmd.cmdStart[i]+1]) != 0){
                perror("");
                exit(0);
            }
            continue;
        }else
        if(search_path(buffer,cmd_name) == 0){
            // Create an unnamed pipe 
            if (pipe(pipefd) == -1) {
                fprintf(stderr, "parent: Failed to create pipe\n");
                return -1;
            }


            tmp_args = cmd.argv + cmd.cmdStart[i];
            while(*tmp_args){
                if(**tmp_args == '$'){
                    *tmp_args = getenv(*tmp_args +1);
                }
                else if(**tmp_args == '`'){
                    int newpipe[2];
                    pipe(newpipe);
                    
                    (*tmp_args)[strlen(*tmp_args) - 1] = '\0';
                    // fprintf(stderr,"%s\n",*tmp_args+1);
                    if(exec_cmd(*tmp_args + 1, newpipe) == 0){
                        char *pbuffer = malloc(sizeof(char) * 1024);
                        if (poll(&(struct pollfd){ .fd = newpipe[READ_END], .events = POLLIN }, 1, 0)==1) {
                            //since read is a blocking syscall, thus in the even of there is no data written to the pipe
                            //the current process will be blocked forever. e.g. if the sub commands return nothing
                            //thus we would check if the pipe is empty before trying to read it.
                            if(read(newpipe[READ_END],pbuffer,1024) > 0){
                                fprintf(stderr,"%s \n",pbuffer);
                                *tmp_args = pbuffer;
                            }else{
                                // fprintf(stderr,"GOT nothing\n");
                                *tmp_args = '\0';
                            }
                        }
                    }
                }
                //parse environment variables inside double quotes
                else if(**tmp_args == '"'){
                    
                    int x=0,i=0; //x for index for out buffer
                    //i for indexing for str buffer

                    char *out = calloc(sizeof(char), 2048);//buffer for holding the result of double quote
                    char *str = malloc(sizeof(char) * strlen(*tmp_args) + 1); //buffer that duplicate the original argv
                    strcpy(str,*tmp_args);
                    // fprintf(stderr, "%s\n",str);
                    while(str[i]){
                        // fprintf(stderr, "%c %d\n",str[i],i);
                        if(str[i] == '$'){ //if env
                            //loop till the next space of the string to get the environment name
                            char *tmpstr = str + i;
                            char bak_char;
                            while(*tmpstr && (*tmpstr != ' ' && *tmpstr != '"')){tmpstr++;}
                            bak_char = *tmpstr;
                            *tmpstr = '\0';
                            
                            //insert the environment value at the $
                            char *envval = getenv(str + i +1);
                            if(envval == NULL){
                                perror("Nu such env variable");
                            }
                            out[x] = '\0';
                            strcat(out,envval);
                            *tmpstr = bak_char;
                            i+= tmpstr - str - i;
                            x+= strlen(envval) ;
                        }
                    //    fprintf(stderr,"%s %d %c %d\n",out,x,out[x],out[x]);
                    //    fprintf(stderr,"%s %d %c %d\n\n",str,i,str[i], str[i]);
                        out[x++] = str[i++];
                    }
                    out[x] = '\0';
                    
                    
                    //free the str buffer, note that out buffer is not freed
                    //as it is used for argv
                    free(str);
                    //delete the the first and last character, which are both ""
                    out[strlen(out) - 1] = '\0';
                    *tmp_args = out +1;
                }
                tmp_args++;
            }

            // tmp_args = cmd.argv + cmd.cmdStart[i];
            //    while(*tmp_args){
            //        fprintf(stderr, "%s\n",*tmp_args++);
            //    }
            
           if(!fork()){ //child
                
                if(dup2(fd_in,STDIN_FILENO) == -1){
                   perror("can't dup pipe write: ");
                   exit(0);
                }
                //if this is not the last command, duplicate the pipe to stdout, so that the next
                //command can use the pipe to read the input
                if (i < cmd.numCommands -1){
                    dup2(pipefd[WRITE_END], STDOUT_FILENO);
                }
                close(pipefd[READ_END]);

                //run command in background
                if(cmd.background){
                    // printf("back");
                    setpgid(0,0);
                }
                //loop through the argv, and interpret the environment variable

                execve(buffer,cmd.argv + cmd.cmdStart[i],NULL);
           }else{
               //parent waiting the child to finish
                wait(&status);
                // if(status != 0){
                //     printf("%s : %s\n",cmd.argv[cmd.cmdStart[i]],strerror(WEXITSTATUS(status)));
                // }
                close(pipefd[WRITE_END]);
                fd_in = pipefd[READ_END]; //save the input for the next command
           }
        }else{
            fprintf(stderr, "%s: ", cmd_name);
            perror("");
        }
    }
    //close all
    if(status != 0){
        printf("%s : %s\n",cmd.argv[cmd.cmdStart[i]],strerror(WEXITSTATUS(status)));
    }
    //restore stdin and stdout
    if(cmd.infile){
        dup2(saved_stdin,STDIN_FILENO);
        close(saved_stdin);
    }
    if(cmd.outfile){
        dup2(saved_stdout,STDOUT_FILENO);
        close(saved_stdout);
    }
    free(buffer);
    return 0;
}

int main(int argc, char **argv){
    char line[LIMIT];
    FILE *f;
    // char **env = malloc(sizeof(*env) * ENV_LEN);
    int env_len = 10;
    int env_index = 0;
    int ret;
    int i;

//    int newpipe[2];
//    pipe(newpipe);
//    if(exec_cmd("ls -l | grep nsh", newpipe) == 0){
//        int pfd;
//        char *pbuffer = malloc(sizeof(char) * 1024);
//        read(newpipe[READ_END],pbuffer,1024);
//        fprintf(stderr,"%s\n",pbuffer);

//    }
//    return 0;
    
    // for(int i=0; i< ENV_LEN; i++){
    //     env[i] = malloc(sizeof(char ) * ENV_CHA_LEN);
    // }
    //ignore ctrl c
    signal(SIGINT, SIG_IGN);
    
    //if a script is provided, interpret each line
    if(argc == 2){
        f = fopen(argv[1],"r");
        
        while(fgets(line,LIMIT,f) > 0){
            ret = exec_cmd(line,NULL);
            if(ret != 0){
                perror("Syntax is wrong");
                exit(0);
            }
        }
        fclose(f);
        return 0;
    }

    char c;

    
    while(1){
        printf("?> ");

        if(!fgets(line,LIMIT,stdin))
            perror("error reading line");


        ret = exec_cmd(line,NULL);
        if(ret != 0){
            perror("Syntax is wrong");
            exit(0);
        }
    }
    return 0;
    
}
