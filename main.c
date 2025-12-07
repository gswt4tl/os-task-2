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
int split_args(char *input, char **argv)
{
    int argc = 0, i = 0, flag = 0;
    
    if (input == NULL || argv == NULL)
    {
        return 0;
    }

    /* пропускаем начальные пробелы */
    while (input[i] == ' ' || input[i] == '\t')
    {
        i++;
    }

    while (input[i] != 0 && input[i] != '\n') 
    {
        if (input[i] == ' ' || input[i] == '\t') 
        {
            if (flag == 1) 
            {
                input[i] = 0;  /* завершаем текущий аргумент */
                flag = 0;
            }
        } 
        else 
        {
            if (flag == 0) 
            {
                argv[argc] = &input[i];  /* вписываем начало нового аргумента/команды */
                argc++;
                flag = 1;
            }
        }
        i++;
    }
    
    /* завершаем последний аргумент */
    if (flag == 1)
    {
        input[i] = 0;
    }
    
    argv[argc] = NULL;  /* NULL для execvp */
    return argc;
}

/* считаем количество '|' */
unsigned int count_pipes(char **argv, int argc, unsigned int pipe_pos[])
{
	unsigned int count = 0;
	for (unsigned int i = 0; i < argc; i++)
	{
		if (argv[i][0] == '|' && argv[i][1] == 0)
		{
			pipe_pos[count] = i;
            count++;
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
int run_multiple(char **argv, int argc, int count, int pipe_pos[]) 
{  	
    /* создаем пайпы для всех команд, кроме последней */
    int pipes[MAX_ARGS][2];
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
    for (int k = 0; k < count + 1; k++) 
    {
        pids[k] = fork();
        if (pids[k] == -1) 
        {
            perror("fork");
            for (int j = 0; j < count; j++) 
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }
        
        if (pids[k] == 0) 
        {
            /* дублируем stdin в пайп для связи с предыдущим процессом */
            if (k > 0) 
            {
                dup2(pipes[k - 1][0], 0);
            }
            
            /* дублируем stdout в пайп текущего процесса */
            if (k < count) 
            {
                dup2(pipes[k][1], 1);
            }
            
            /* закрываем ненужные пайпы */
            for (int j = 0; j < count; j++) 
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            /* выборка команд/аргументов  */
            int start_index, finish_index;
            if (k == 0)  /* первая команда */
            {
                start_index = 0;
                finish_index = pipe_pos[0];
            } 
            else if (k == count)  /* последняя команда */
            {
                start_index = pipe_pos[k - 1] + 1;
                finish_index = argc;
            }
            else  /* команда в середине */
            {
                start_index = pipe_pos[k - 1] + 1;
                finish_index = pipe_pos[k];
            }
            
            /* массив аргументов для текущей команды */
            char *current_argv[MAX_ARGS];
            int current_argc = 0;
            
            for (int i = start_index; i < finish_index; i++) 
            {
                current_argv[current_argc] = argv[i];
                current_argc++;
            }
            current_argv[current_argc] = NULL;

            execvp(current_argv[0], current_argv);
            fprintf(stderr, "Не смогли запустить программу '%s': %s\n", current_argv[0], strerror(errno));
            exit(-1);
        }
    }
    
    /* закрываем пайпы в отце */
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
            ret_val++;
        }
    }
    
    return ret_val;
}


int execute_commands(char *input) 
{
    if (input == NULL || input[0] == 0 || input[0] == '\n')
    {
        return 0;
    }

    /* разбиваем строку на аргументы */
    char *argv[MAX_ARGS];
    int argc = split_args(input, argv);
    
    if (argc == 0)
    {
        return 0;  /* пустая команда */
    }

    unsigned int pipe_pos[MAX_ARGS];
    unsigned int count = count_pipes(argv, argc, pipe_pos);
    
    if (count == 0)
    {
		return run_command(argv);
	}
	else if (count > 0)
	{
		return run_multiple(argv, argc, count, pipe_pos);
	}
    else
    {
        return 1;
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

    printf("\e[1;38;5;211m%s\e[0m:\e[1;38;5;5m%s\e[0m$ ", pw->pw_name, path);
   
    /* ввод команд в stdin  */
    char input[MAX_STR_SIZE];
    while (fgets(input, MAX_STR_SIZE, stdin) != NULL)
    {
        if (strcmp(input, "exit\n") == 0)
        {
            break;
        }
        
        if (execute_commands(input) == 1)
        {
            fprintf(stderr, "Произошла ошибка при выполнении команд.\n");
        }
        
        printf("\e[1;38;5;211m%s\e[0m:\e[1;38;5;5m%s\e[0m$ ", pw->pw_name, path);
    }

    printf("\e[2J\e[H");
    return 0;
}
