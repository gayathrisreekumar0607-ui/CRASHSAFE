#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum{
    MAX_WORKERS=4,
    BUFFER=4096
}; 
typedef enum{
    PENDING,
    INPROGRESS,
    COMPLETED,
    FAILED
}transferStatus;

typedef struct{
    const char *source;
    const char *destination;
    transferStatus status;
    uintmax_t bytes;
} transferTask;

typedef struct{
    transferTask *tasks;
    int count;
    int next;
    pthread_mutex_t mutex;
}taskLIST;

static int verify(const char *source, const char *destination);

//file copying. 
static int copy_bytes(int source, int destination, transferTask *task){
    char buffer[BUFFER];
    ssize_t count;
    while( (count=read(source,buffer,sizeof buffer))>0){
        ssize_t written=0; 
        while(written<count){
            ssize_t n=write(destination,buffer+written,(size_t)(count-written));
            if(n<=0){
                if(n<0){
                    perror("error occurded in writing");
                }else{
                    fprintf(stderr, "write made no progress\n");
                }
                return -1;
            }
            written+=n;
            task->bytes+=(uintmax_t)n;
        }
    }
    if(count<0){
        perror("error in reading");
        return -1;
    }
    return 0;
}



static int copy_file(transferTask *task){
    struct stat info;
    if(stat(task->source,&info)<0){
        perror(task->source);
        return -1;
    }
    if(!S_ISREG(info.st_mode)){
        fprintf(stderr,"%s is not a regular file\n",task->source);
        return -1;
    }
    int source=open(task->source,O_RDONLY);
    if(source<0){
        perror(task->source);
        return -1;
    }
    int destination=open(task->destination,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(destination<0){
        perror(task->destination);
        close(source);
        return -1;
    }
    int result=copy_bytes(source,destination,task);
    if(close(source)<0){
        perror("close source");
        result=-1;
    }
    if(close(destination)<0){
        perror("close destination");
        result=-1;
    }
    if(result==0){
        result=verify(task->source,task->destination);
    }
    if(result<0&&unlink(task->destination)<0){
        perror("remove incomplete copy");
    }
    return result;
}


static int verify(const char *source, const char *destination)
{
    FILE *a=fopen(source, "rb");
    if(!a){
        perror(source);
        return -1; 
    }
    FILE *b=fopen(destination, "rb");
    if(!b){
        perror(destination);
        fclose(a); 
        return -1; 
    }

    int left,right;
    do{
        left=fgetc(a);
        right=fgetc(b);
    } while(left == right && left != EOF);
    int matches=left==right&&!ferror(a)&&!ferror(b);
    if(fclose(a)==EOF){
        matches=0;
    }
    if(fclose(b)==EOF){
        matches=0;
    }
    if(!matches){
        fprintf(stderr,"the verification faield:%s ->%s \n ",source,destination);
    }
    return matches? 0 : -1;
}

static void check_pthread(int error, const char *operation){
    if(error!=0){
        fprintf(stderr, "%s: %s \n ",operation, strerror(error));
        exit(EXIT_FAILURE);
    }
}


static void *worker(void *arg){
    taskLIST *list=arg;
    while(1){ //want it to run infinitely until exit statement
        check_pthread(pthread_mutex_lock(&list->mutex), "lock the task lists");
        if(list->next>=list->count){
            check_pthread(pthread_mutex_unlock(&list->mutex), "unlock the task lists");
            return NULL;
        }



        transferTask *task=&list->tasks[list->next++];
        check_pthread(pthread_mutex_unlock(&list->mutex),"unlock the tassk");
        task->status=INPROGRESS;
        task->status=copy_file(task)==0? COMPLETED : FAILED;
    }
}


//making the final report
static const char *STATUS(transferStatus status){
    switch(status){
        case PENDING:
            return "Pending";
        case INPROGRESS:
            return "In progress";
        case COMPLETED:
            return "Completed";
        case FAILED:
            return "Failed";
    }
    return "unknown";
}

static int printReport(const taskLIST *list, int start){
    int succeeded=0; 
    uintmax_t bytes=0;
    for(int i=0;i<list->count; i++){
        const transferTask *task=&list->tasks[i];
        printf(" %s -> %s | %s | %ju bytes. \n\n", task->source,task->destination, STATUS(task->status),task->bytes);
        bytes+=task->bytes;
        succeeded+=task->status== COMPLETED;
    }
    printf("there are %d succeeded, %d failed | %ju bytes written successfully with %d works started \n ",succeeded, list->count - succeeded, bytes, start);
    return succeeded==list->count? 0 : 1;
}

//entire MAIN function is for reading the command line the user enteres and identifying which functionsto implement and when. 
int main(int arg_count, char *arg_array[]){ //count of arguments, array of entered arguments
    //verification first
    if(arg_count>1&&strcmp(arg_array[1],"verify")==0){
        if(arg_count==4){
            return verify(arg_array[2], arg_array[3]) !=0;
        }
        fprintf(stderr,"usage: %s verify SOURCE DESTINATION\n",arg_array[0]);
        return 1;
    }

    int first=1; //first index since the 0th index is the filename "crashsafe"
    int workers=MAX_WORKERS;
    if(first+1<arg_count&&strcmp(arg_array[first], "-j")==0){
        const char *count=arg_array[first+1];
        if(strlen(count)!=1||count[0]<'1'||count[0]-'0'> MAX_WORKERS){
            fprintf(stderr,"error: worker count must be between 1 to %d \n ", MAX_WORKERS);
            return 1;
        }
        workers=count[0]-'0'; //ascii value finding for changing the character into int type. 
        first+=2;
    }

    //real file work
    int paths=arg_count-first;
    if(paths<2||paths%2!=0){
        fprintf(stderr,"incorrect number in the number of sources/destinations. \n" ); //error
        return 1;
    }

    taskLIST list={0};
    list.count=paths/2;
    list.tasks=calloc((size_t)list.count,sizeof *list.tasks);
    if(!list.tasks){
        perror("allocate the tasks among workers");
        return 1;
    }
    for(int i=0;i<list.count;i++){
        list.tasks[i].source=arg_array[first+2*i];
        list.tasks[i].destination= arg_array[first+2*i+1];
        list.tasks[i].status=PENDING;
    }

    check_pthread(pthread_mutex_init(&list.mutex,NULL),"initialise the mutex here");
    pthread_t threads[MAX_WORKERS];
    int start=0;
    for(;start<workers;start++){
        int error=pthread_create(&threads[start],NULL,worker, &list);
        if(error){
            fprintf(stderr,"create worker %s \n ",strerror(error));
            break;
        }
    }
    if(start==0){
        worker(&list);
    }
    for(int i=0;i<start;i++){
        check_pthread(pthread_join(threads[i],NULL),"join the worker ");
    }
    int result=printReport(&list,start);
    check_pthread(pthread_mutex_destroy(&list.mutex),"destroying the mutex ");
    free(list.tasks);
    return result;
}
