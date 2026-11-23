#include <stdio.h>
#include <fcntl.h> //file control for open function. 
#include <unistd.h> //unix standard for close function
#include <inttypes.h>
#include <sys/stat.h>

static int copy_bytes(int source_fd, int destination_fd){//static makes the function local to main function. 
    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read=read(source_fd, buffer, sizeof buffer))>0){
        ssize_t bytes_written=0; 
        while(bytes_written<bytes_read){
            ssize_t result= write(destination_fd, buffer+bytes_written, (size_t)(bytes_read-bytes_written));
            if(result==-1){
                perror("count not write to destination");
                return -1;
            }
            if(result==0){
                fprintf(stderr, "write made no progress");
                return -1;
            }
            bytes_written+=result;
        }
    }
    if(bytes_read==-1){
        perror("could not read source");
        return -1;
    }
    return 0;
}

static int copy_file(const char *source_path, const char *destination_path){

    struct stat source_info;
    if(stat(source_path, &source_info)==-1){
        perror(source_path);
        return -1;
    }
    if(!S_ISREG(source_info.st_mode)){
        fprintf(stderr, "%s is not a regular file\n", source_path);
        return -1;
    }

    int source_fd=open(source_path, O_RDONLY);
   if(source_fd==-1){//means if it doesnt work and returns -1 for some error
    perror(source_path);
    return -1;
   }

    int destination_fd=open(destination_path, O_WRONLY|O_CREAT|O_EXCL, 0600);

    if(destination_fd==-1){
        perror(destination_path);
        close(source_fd);
        return -1;
    }
    int copy_result=copy_bytes(source_fd, destination_fd);
    if(close(source_fd)==-1){                       //if the file could not be closed, and hence it prints -1. then
        perror("could not close source");
        copy_result=-1;
    }
    if(close(destination_fd)==-1){
        perror("could not close destination");
        copy_result=-1;
    }
    if(copy_result==-1){
        if (unlink(destination_path)==-1) {
            perror("could not remove incomplete destination");
        }
        return -1;
    }
    printf("copied %s to %s (and there are %jd bytes in source at start)\n",
           source_path, destination_path, (intmax_t)source_info.st_size);
    return 0;
}




int main(int argc, char *argv[]) //argc counts the number of entries. arg[] reads everything printed on that line. argv[1] 
//is the source file. argv[2] is the destination file
{
    if (argc<3 || ((argc-1)%2 !=0)){
        fprintf(stderr, "usage : %s source destination", argv[0]);
        return 1; //prints the usage message to stderr which is the terminal stream for error messages
    }
    
    int exit_status=0;
    for(int index=1; index<argc; index+=2){
        if(copy_file(argv[index], argv[index+1])==-1){
            exit_status =1;
        }
    }
    return exit_status;
}
