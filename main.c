#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <limits.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_STR_SIZE 1024
#define MAX_ARGS 256


/* разбиение строки на аргументы */
int split_args(char *input, char **argv, int max_args) 
{
    int argc = 0, i = 0, in_arg = 0;
    
    if (input == NULL || argv == NULL)  return 0;
    
    /* пропускаем начальные пробелы */
    while (input[i] == ' ')  i++;
    
    while (input[i] != 0 && input[i] != '\n' && argc < max_args - 1) 
    {
        if (input[i] == ' ') 
        {
            if (in_arg) 
            {
                input[i] = 0;  /* завершаем текущий аргумент */
                in_arg = 0;
            }
        } 
        else 
        {
            if (!in_arg) 
            {
                argv[argc++] = &input[i];  /* начало нового аргумента */
                in_arg = 1;
            }
        }
        i++;
    }
    
    /* завершаем последний аргумент */
    if (in_arg && i > 0)
    {
        input[i] = 0;
    }
    
    argv[argc] = NULL;  /* NULL для execvp */
    return argc;
}


/* считаем кол-во '|' */
int count_commands(char **argv, int argc, int pipe_positions[]) 
{
    int count = 0;
    for (int i = 0; i < argc; i++) 
    {
        if (argv[i][0] == '|' && argv[i][1] == 0) 
        {
            pipe_positions[count++] = i;
        }
    }
    return count;
}


/* выполнение одиночной команды */
int run_command(char **argv) 
{
    pid_t pid = fork();
    
    if (pid == -1) 
    {
        perror("fork");
        return 1;
    }
    
    if (pid == 0) 
    {
        execvp(argv[0], argv);
        fprintf(stderr, "Не смогли запустить программу '%s': %s\n", argv[0], strerror(errno));
        exit(-1);
    }
    
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status) && (WEXITSTATUS(status) == (signed char)-1)) 
    {
        return 1;
    }
    
    return 0;
}

/* выполнение конвейера команд */
int run_conveyor(char **argv, int argc, int count, int pipe_positions[]) 
{  
    int pipes[MAX_ARGS][2];
    
    /* создаем пайпы для всех команд, кроме последней */
    for (int i = 0; i < count; i++) 
    {
        if (pipe(pipes[i]) == -1) 
        {
            perror("pipe");
            return 1;
        }
    }
    
    /* создаем процессы для всех команд */
    pid_t pids[MAX_ARGS];
    
    for (int cmd_idx = 0; cmd_idx < count + 1; cmd_idx++) 
    {
        pids[cmd_idx] = fork();
        
        if (pids[cmd_idx] == -1) 
        {
            perror("fork");
            for (int j = 0; j < cmd_idx; j++) 
            {
                kill(pids[j], SIGTERM);
            }
            for (int j = 0; j < count; j++) 
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }
        
        if (pids[cmd_idx] == 0) 
        {
            if (cmd_idx > 0) 
            {
                dup2(pipes[cmd_idx - 1][0], STDIN_FILENO);
            }
            
            if (cmd_idx < count) 
            {
                dup2(pipes[cmd_idx][1], STDOUT_FILENO);
            }
            
            for (int j = 0; j < count; j++) 
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            int start_idx, end_idx;
            
            if (cmd_idx == 0) 
            {
                /* первая команда */
                start_idx = 0;
                end_idx = pipe_positions[0];
            } 
            else if (cmd_idx == count) 
            {
                /* последняя команда */
                start_idx = pipe_positions[cmd_idx - 1] + 1;
                end_idx = argc;
            } 
            else 
            {
                /* команда в середине */
                start_idx = pipe_positions[cmd_idx - 1] + 1;
                end_idx = pipe_positions[cmd_idx];
            }
            
            /* массив аргументов для текущей команды */
            char *cmd_argv[MAX_ARGS];
            int cmd_argc = 0;
            
            for (int i = start_idx; i < end_idx; i++) 
            {
                cmd_argv[cmd_argc++] = argv[i];
            }
            cmd_argv[cmd_argc] = NULL;

            execvp(cmd_argv[0], cmd_argv);
            fprintf(stderr, "Не смогли запустить программу '%s': %s\n", cmd_argv[0], strerror(errno));
            exit(-1);
        }
    }
    
    for (int i = 0; i < count; i++) 
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    int status;
    unsigned int ret_val = 0;
    
    for (int i = 0; i < count + 1; i++) 
    {
        waitpid(pids[i], &status, 0);
        
        if (WIFEXITED(status) && (WEXITSTATUS(status) == (signed char)-1)) 
        {
            ret_val |= (1 << i);
        }
    }
    
    return (ret_val << 4);
}


int execute_commands(char *input) 
{
    if (input == NULL || input[0] == 0 || input[0] == '\n')  return 0;
    
    /* копия строки для разбора аргументов */
    char input_copy[MAX_STR_SIZE];
    strncpy(input_copy, input, MAX_STR_SIZE - 1);
    input_copy[MAX_STR_SIZE - 1] = 0;
    
    /* разбиваем строку на аргументы */
    char *argv[MAX_ARGS];
    int argc = split_args(input_copy, argv, MAX_ARGS);
    
    if (argc == 0)  return 0;  /* пустая команда */
    
    /* находим позиции '|' */
    int pipe_positions[MAX_ARGS];
    int count = count_commands(argv, argc, pipe_positions);
    
    if (count == 0) 
    {
        return run_command(argv);
    } 
    else 
    {
        return run_conveyor(argv, argc, count, pipe_positions);
    }
}


int main()
{
    printf("\e[2J\e[H");

    /* получили имя пользователя  */ 
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (pw == NULL)
    {
        perror("getpwuid");
        return 1;
    }

    /* получили рабочий каталог */
    char path[PATH_MAX];
    if (getcwd(path, PATH_MAX) == NULL)
    {
        perror("getcwd");
        return 2;
    }

    /* замена /home/local_user/ на ~ */
    size_t homedir_len = strlen(pw->pw_dir);
    if (strncmp(pw->pw_dir, path, homedir_len) == 0)
    {
        char new_path[PATH_MAX];
        new_path[0] = '~';

        if (path[homedir_len] != 0) 
        {
            strcpy(new_path + 1, path + homedir_len);
        } 
        else 
        {
            new_path[1] = 0;
        }
        
        strcpy(path, new_path);
    }

    printf("\e[1;32;49m%s\e[0m:\e[1;34;49m%s\e[0m$ ", pw->pw_name, path);
   
    /* ввод команд в stdin  */
    char input[MAX_STR_SIZE];
    while (fgets(input, MAX_STR_SIZE, stdin) != NULL)
    {
        if (strcmp(input, "exit\n") == 0)
        {
            break;
        }
        
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') 
        {
            input[len - 1] = 0;
        }
        
        execute_commands(input);
        
        printf("\e[1;32;49m%s\e[0m:\e[1;34;49m%s\e[0m$ ", pw->pw_name, path);
    }

    printf("\e[2J\e[H");
    return 0;
}
