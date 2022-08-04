#define _POSIX_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>


static volatile sig_atomic_t bgCheck = 0;
static volatile sig_atomic_t fgOnlyMode = 0;
static volatile sig_atomic_t sigChildPid = 0;

// handle SIGTSTP signal (toggle fgOnlyMode)
void handle_SIGTSTP_1()
{
  if( sigChildPid != 0 && ( bgCheck == 0 || fgOnlyMode == 1 )) 
  {
    //while ((sigChildPid = waitpid(-1, NULL, 0)) > 0);
    while( waitpid( -1, NULL, 0 ) > 0 );
  }
  sigChildPid = 0;
  char* message = "\nentering foreground only mode (& ignored)\n";
  write( STDOUT_FILENO, message, 43 );
  fgOnlyMode = 1;
}


void handle_SIGTSTP_2()
{
  //while ((sigChildPid = waitpid(-1, NULL, 0)) > 0);
  while( waitpid( -1, NULL, 0 ) > 0 );
  sigChildPid = 0;
  char* message = "\nexiting foreground only mode\n";
  write( STDOUT_FILENO, message, 30 );
  fgOnlyMode = 0;
}



int
main ()
{ 
  int saveStdOut = dup( 1 ); 
  int saveStdIn = dup( 0 );
  char status[32] = "exit status 0";
  int numBgPids = 0;
  pid_t bgPids[32];
  memset( bgPids, 0, sizeof( bgPids ));
  int fgChildStatus;
  int bgChildStatus;
  pid_t childPid;

  ///////////////////////////////////////////////////////////////
  //                 outer loop (reprompt)
  ///////////////////////////////////////////////////////////////

  while (1) 
  {
    // declare and initialize variables
    int argSize = 15;
    char inputArray[512][argSize];
    memset( inputArray, 0, sizeof ( inputArray ));
    int arrayCounter = 0;
    size_t stringIndex = 0;
    int spaceCheck;

    int numArgsIn = 0;
    int numArgsExec = 0;

    const char* intToChar = "0123456789";
    char pidString[argSize];
    memset( pidString, 0, sizeof( pidString ));
    int shellPid = 0;

    char command[argSize];
    memset( command, 0, sizeof( command ));

    char newIn[argSize];
    memset( newIn, 0, sizeof( newIn ));

    char newOut[argSize];
    memset( newOut, 0, sizeof( newOut ));

    char *args[512];
    memset( args, 0, sizeof( args ));
    int argsCounter = 0;

    char *bg = "&";
    char *redirectIn = "<";
    char *redirectOut = ">";

    bgCheck = 0;

    /////////////////////////////////////////////////////////////
    //           waitpid loop to clean up bg processes
    /////////////////////////////////////////////////////////////

    for( int i = 0; i < 32; i++ )
    {
      if( bgPids[i] == 0 )
      {
        continue;
      }

      pid_t w = 0;
      w = waitpid( bgPids[i], &bgChildStatus, WNOHANG );

      if( w == -1 )
      {
        perror( "waitpid" );
        exit(  EXIT_FAILURE );
      }

      if( w == 0 ) 
      {
        continue;
      }


      if( WIFEXITED( bgChildStatus ))
      {
        printf( "background pid %d is done: exit status %d\n", bgPids[i], WEXITSTATUS( bgChildStatus ));
        fflush( stdout );
      } 

      else if( WIFSIGNALED( bgChildStatus )) 
      {
        printf( "background pid %d is done: terminated by signal %d\n", bgPids[i], WTERMSIG( bgChildStatus ));
        fflush( stdout );
      } 
      else if( WIFSTOPPED( bgChildStatus )) 
      {
        printf( "stopped by signal %d\n", WSTOPSIG( bgChildStatus ));
      } 
      else if( WIFCONTINUED( bgChildStatus ))
      {
        printf( "continued\n" );

      }
      bgPids[i] = 0;
    }

    // SIGINT handling (parent and bg child ignore)
    struct sigaction SIGINT_ignore_action = {0};
    SIGINT_ignore_action.sa_handler = SIG_IGN;
    sigaction( SIGINT, &SIGINT_ignore_action, NULL );

    // SIGTSTP handling (set/unset fgOnly Mode)
    struct sigaction SIGTSTP_action_1 = {0}, SIGTSTP_action_2 = {0};
    SIGTSTP_action_1.sa_handler = handle_SIGTSTP_1;
    SIGTSTP_action_1.sa_flags = 0;
    SIGTSTP_action_2.sa_handler = handle_SIGTSTP_2;
    SIGTSTP_action_2.sa_flags = 0;

    if( fgOnlyMode == 0 )
    {
      sigaction( SIGTSTP, &SIGTSTP_action_1, NULL );
    }

    if( fgOnlyMode == 1 )
    {
      sigaction( SIGTSTP, &SIGTSTP_action_2, NULL );
    }

    dup2( saveStdOut, 1 );  
    dup2( saveStdIn, 0 );

    // prompt
    printf( ": " );
    fflush( stdout );

    ///////////////////////////////////////////////////////////
    //                 parse user input
    ///////////////////////////////////////////////////////////

    char c;
    while( 1 )
    {
      c = fgetc( stdin );
      inputArray[arrayCounter][stringIndex] = c;  

      if( c == '\n' )
      {
        if( spaceCheck == 0 )
        {
          numArgsIn++;
        }
        break;
      }

      if( c == ' ' )
      {   
        if( spaceCheck == 0 )
        { 
          spaceCheck = 1;
          inputArray[arrayCounter][stringIndex] = 0;  
          arrayCounter++;
          numArgsIn++;
        }

        else
        {
          spaceCheck = 0;
        }
        stringIndex = 0;
        continue;
      }

      spaceCheck = 0;

      // '$$' expansion
      if( c == '$' )
      {
        c = fgetc( stdin );
        if( c == '$' )
        {
          shellPid = getpid();
          int i = 0;
          int pidLength = 0;

          // convert pid to string (in reverse)
          while( shellPid != 0 )
          {
            int digit = shellPid % 10;
            pidString[i++] = intToChar[digit];
            shellPid = shellPid / 10;
            pidLength++;
          }

          // add pid to inputArray
          for( i = 1; i <= pidLength; i++ )
          {
            inputArray[arrayCounter][stringIndex++] = pidString[pidLength - i];
          }  
          continue;          
        }
        ungetc( c, stdin );
      }
      stringIndex++;
    }

    // handle blank lines and comments
    if( inputArray[0][0] == '\n' || inputArray[0][0] == '#' || inputArray[0][0] == ' ' )
    {
      numArgsIn = 0;
      continue;
    }

    // remove '\n' from input
    inputArray[arrayCounter][stringIndex] = 0;

    ///////////////////////////////////////////////////////////
    //   sort input words (command, args, redirection, fg/bg)
    ///////////////////////////////////////////////////////////

    // save command
    strcpy( command, inputArray[0] );
    args[0] = command;
    argsCounter++;
    numArgsExec++;

    // check for '&'
    if( strcmp( inputArray[numArgsIn - 1], bg ) == 0 )
    {
      numArgsIn--;
      if( fgOnlyMode == 0 )
      {
        bgCheck = 1;
      }
    }

    // handle other args
    for( int i = 1; i < numArgsIn; i++ )
    {
      // check for and handle i/o redirection
      if( strcmp( inputArray[i], redirectIn ) == 0 )
      {
        strcpy( newIn, inputArray[i + 1] );
        i++;
      }

      else if( strcmp( inputArray[i], redirectOut ) == 0 )
      {
        strcpy( newOut, inputArray[i + 1] );
        i++;
      }

      // otherwise, add to args array
      else 
      {
        args[argsCounter] = inputArray[i];
        numArgsExec++;
        argsCounter++; 
      }  
    }  

    strcat( args[argsCounter], "\0" );

    ///////////////////////////////////////////////////////////
    //        check for and handle built-in commands
    ///////////////////////////////////////////////////////////

    // handle cd
    if( strcmp( command, "cd" ) == 0 )
    {
      char *homeDir;

      if( numArgsExec == 1 )
      {
        homeDir = getenv( "HOME" );
        chdir( homeDir );
      }

      else
      {
        chdir( args[1] );
      }
    }  

    // handle status
    else if( strcmp( command, "status" ) == 0 )
    {
      printf( "%s\n", status );
      fflush( stdout );
      continue;
    }

    // handle exit
    else if( strcmp( command, "exit" ) == 0 )
    {
      exit( 0 );
    }

        
    //////////////////////////////////////////////////////////
    //     handle other commands with fork() and execvp()
    //////////////////////////////////////////////////////////

    else
    {
      childPid = fork();
      if( childPid == -1 )
      {
        perror( "fork() failed!" );
        exit( 1 );
      } 

      /////////////////////////////////////////////////////////
      //                   child process
      /////////////////////////////////////////////////////////
      else if( childPid == 0 )
      {
        if( bgCheck == 0 || fgOnlyMode == 1 ) 
        { 

          //fg SIGINT handling
          struct sigaction SIGINT_fg_action = {0};
          SIGINT_fg_action.sa_handler = SIG_DFL;

          sigaction( SIGINT, &SIGINT_fg_action, NULL );
        }

        // child SIGTSTP handling pos 2
        struct sigaction ignore_action = {0};
        ignore_action.sa_handler = SIG_IGN;

        sigaction( SIGTSTP, &ignore_action, NULL );

        // redirect in
        if( strcmp( newIn, "" ))
        {
          int inFD = open( newIn, O_RDONLY );
          if( inFD == -1 ) 
          {
            perror( "open() (in)" );
            exit( 1 );
          }
          int result = dup2( inFD, 0 );
          if( result == -1 ) 
          {
            perror( "dup2" ); 
            exit( 2 ); 
          }
        }

        if( strcmp( newIn, "" ) == 0 && bgCheck != 0 )
        {
          int inFD = open( "/dev/null", 0 );
          if( inFD == -1 )
          {
            perror( "open() (in)" );
          }
          int result = dup2( inFD, 0 );
          if( result == -1 )
          {
            perror( "dup2" );
            exit( 2 );
          }
        }

        // redirect out
        if( strcmp( newOut, "" ))
        {
          int outFD = open( newOut, O_WRONLY | O_CREAT | O_TRUNC, 0640 );
          if ( outFD == -1 ) 
          {
            perror( "open() (out)" );
            exit( 1 );
          }
          int result = dup2( outFD, 1 );
          if( result == -1 ) 
          {
            perror( "dup2" ); 
            exit( 2 ); 
          }
        }

        if( strcmp( newOut, "" ) == 0 && bgCheck != 0 )
        {
          int outFD = open( "/dev/null", O_WRONLY );
          if( outFD == -1 )
          {
            perror( "open() (out)" );
          }
          int result = dup2( outFD, 1 );
          if( result == -1 )
          {
            perror( "dup2" );
            exit( 2 );
          }
        } 

        // execute
        args[numArgsExec] = NULL;
        execvp( command, (char* const*) args );
        perror( "execvp()" );   
        exit( EXIT_FAILURE );       
      }

      ////////////////////////////////////////////////////////
      //                 parent process
      ////////////////////////////////////////////////////////

      else 
      { 
        if( bgCheck == 0 || fgOnlyMode == 1 ) 
        { 
          // handle fg child
          sigChildPid = childPid;

          // wait for fg child to exit or terminate, update status
          waitpid( childPid, &fgChildStatus, 0 );
          memset( status, 0, sizeof( status ));

          if( WIFEXITED( fgChildStatus ))
          {
            sprintf( status, "exit status %d", WEXITSTATUS( fgChildStatus ));
          }

          else
          {
            sprintf(status, "terminated by signal %d", WTERMSIG(fgChildStatus));
          }

          if( strcmp( status, "terminated by signal 2" ) == 0 )
          { 
            printf( " terminated by signal 2\n" ); 
            fflush( stdout );
          }
        }

        else
        {
          // handle bg child
          bgPids[numBgPids] = childPid;
          printf( "background pid is %d\n", bgPids[numBgPids] );
          fflush( stdout );

          numBgPids++;
          waitpid( childPid, &bgChildStatus, WNOHANG ); 
        }
      } 
    } 
  }
  return( 0 );
}
