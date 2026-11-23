#include <stdio.h>
#include <fcntl.h> //file control for open function. 
#include <unistd.h> //unix standard for close function
#include <inttypes.h>
#include <sys/stat.h>
#include <pthread.h> //for the threads compiling
#include <stdlib.h> //added for module 2
#include <string.h> //added for module 2


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

enum{MAX_WORKERS=4};

typedef struct{
    const char *source_path;
    const char *destination_path;
    int result; //stores wheter the copy succeeded or failed. 0-succeed, 1-failed
} TransferTask;

//keeps track of all the files that need to be copied. 
typedef struct {
    TransferTask *tasks; //full array list of all transfer jobs
    int task_count; //total numbers of files to copy
    int next_task; //tracks which files havent been claimed yet. 
    pthread_mutex_t mutex; //mutex. 
} taskList;

//this is just a safety function for threads. standard thread functions like locking or unlocking a mutex only returns an error when they 
//fail. this function checks if anyting went wrong and if it did, it prints a readable error message stderr(error) explaining what happens. 
static void check_pthread(int error, const char *work){
    if(error!=0){
         fprintf(stderr ,"%s: %s\n", work, strerror(error));
        exit(EXIT_FAILURE); //immediately exits after. 
    }
}

// so so so important. learn. 
static void *worker(void *argument)
{
    taskList *list=argument;
    while(1){
        check_pthread(pthread_mutex_lock(&list->mutex) ,"lock the task list"); //the thread calls the 
        if (list->next_task==list->task_count){ //checks if all the files have already been claimed??. if yes, unlocks mutex. 
            check_pthread(pthread_mutex_unlock(&list->mutex),"unlock thetask list"); //unlock mutex. 
            return NULL;
        }
        TransferTask *task =&list->tasks[list->next_task];
        list->next_task++; //takes the point to the current thread and points (increments it) it to the next thread
        check_pthread(pthread_mutex_unlock(&list->mutex) ,"unlock the task list"); //calls it so that the other threads can get their work
        task->result=copy_file(task->source_path, task->destination_path); //calls this so that multiple files can be copied parallely. stores outcome in task->result. 
    }
}


//argc counts the number of entries. arg[] reads everything printed on that line. argv[1] is the source file. argv[2] is the destination file
int main(int argc, char *argv[])
{
    int count_worker=MAX_WORKERS;
    int first_path=1;
    if(argc>1&&strcmp(argv[1], "-j")==0){
        if (argc<3||strlen(argv[2])!=1 ||
            argv[2][0]<'1'||argv[2][0]>'4') {
            fprintf(stderr, "-j requires a worker count from 1 to 4\n");
            return 1;
        }
        count_worker=argv[2][0]-'0';
        first_path=3;
    }
    int path_count=argc-first_path;
    if(path_count<2||path_count% 2!=0){
        fprintf(stderr, "the usage: %s [-j 1-4] source destination [SOURCE DESTINATION ...]\n",
                argv[0]);
        return 1;
    }
    taskList list={.task_count=path_count/2 };
    list.tasks = calloc((size_t)list.task_count, sizeof *list.tasks);
    if(list.tasks==NULL){
        perror("allocates the task list");
        return 1;
    }
    for(int i=0; i<list.task_count;i++){
        list.tasks[i].source_path=argv[(first_path+2)*i];
        list.tasks[i].destination_path=argv[(first_path+2)*i+1];
    }
    check_pthread(pthread_mutex_init(&list.mutex, NULL),"initialize the  mutex");

    pthread_t threads[MAX_WORKERS];
    int started=0;
    while(started<count_worker){
        int error=pthread_create(&threads[started], NULL, worker, &list);
        if (error!=0){
            fprintf(stderr, "Could not create worker %d: %s\n",
                    started+1, strerror(error));
            break;
        }
        started++;
    }

    if (started==0){
        fprintf(stderr, "Main thread will process the tasks\n"); 
        worker(&list);
    }
    for (int i=0;i<started;i++) {
        check_pthread(pthread_join(threads[i], NULL), "Join the worker");
    }
     int succeeded=0;
    for (int i=0;i<list.task_count;i++) {
        TransferTask *task=&list.tasks[i];
        if (task->result==0) {
            succeeded++;
        } else {
            fprintf(stderr, "failed:%s -> %s",task->source_path, task->destination_path);
            fprintf(stderr, "\n");
        }
    }
    int failed=list.task_count-succeeded;
    printf("summary:%d succeeded, %d failed ( %d workers started)\n",
           succeeded, failed, started);

    check_pthread(pthread_mutex_destroy(&list.mutex), "Destroy mutex");
    free(list.tasks);
    return failed==0 ? 0 : 1;
}
