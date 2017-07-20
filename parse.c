#include "parse.h"

/*
** Some little helper functions (could be macros, but functions are safer).
*/
static int isShellSymb(char c) {
    return (c)=='|' || (c)=='<' || (c)=='>' || (c)== '\'' || (c) == '=' || (c) == '"' || (c) == '`';
}
static int isSeparator(char c) {
    return isspace(c) || isShellSymb(c);
}

/*
** Parse: parse a simple nsh command line, and put the results into sc.
** INPUT: char *line, a string containing whitespace-separated tokens.
** OUTPUT: struct commandLine *sc
** NOTE: No syntax checking occurs.  Empty commands are not detected,
**      for example, so the calling function needs to do syntax checking.
** RETURN VALUES: 0 on success.  (Currently it always returns 0.)
*/
int Parse(char *line, struct commandLine *sc)
{
    int argc = 0;

    assert(strlen(line) < MAX_LINE);
    strcpy(sc->buf, line);
    line = sc->buf;

    sc->infile = sc->outfile = sc->argv[0] = NULL;
    sc->append = sc->numCommands = sc->background = 0;
    sc->cmdStart[0] = 0;    /* the 0th command starts at argv[0] */
    sc->env = sc->env_val =  NULL;

    
    /*
    ** This is the main loop, reading arguments into argv[].
    ** We check against MAX_ARGS-1 since the last one must be NULL.
    */
    while(argc < MAX_ARGS-1 && sc->numCommands < MAX_COMMANDS)
    {
        /*
        ** At this point, "line" is pointing just beyond the end of the
        ** previous argument (or the very first character if this is
        ** the first time through the loop), which should be a whitespace
        ** character.
        */

        while(isspace(*line)) /* skip initial whitespace */
            ++line;
        if(*line == '\0')   /* end-of-line, we're done */
            break;

        /*
        ** At this point, "line" is pointing at a non-space character; it
        ** may be the start of an argument, or it may be a special character.
        */
        
        switch(*line)
        {
        case '|':
            /* terminate the previous argument and bump to next character */
            *line++ = '\0';
            sc->argv[argc++] = NULL;    /* terminate the command */
            /* the next command will start at this position */
            sc->cmdStart[++sc->numCommands] = argc;
            continue;   /* go back to the top of the while loop */

        case '<':
            *line++ = '\0'; /* terminate argument and go to next character */
            while(isspace(*line))   /* skip whitespace */
                ++line;
            sc->infile = line;
            break;

        case '>':
            *line++ = '\0'; /* terminate the argument */
            if(*line == '>') /* second '>' means append */
            {
                sc->append = 1;
                line++;
            }
            while(isspace(*line))   /* skip whitespace */
                ++line;
            sc->outfile = line;
            break;
        case '&':
            sc->background = 1;
            break;
        case '\\':
            sc->argv[argc++] = ++line;
            line++;
            break;
        case '\'':
            sc->argv[argc++] = ++line;
            while(*line != '\''){line++;}
            *line = '\0';
            break;
        case '"':
            sc->argv[argc++] = line++;
            while(*line != '"'){line++;}
            *++line = '\0';
                line++;
                continue;
        case '`':
            sc->argv[argc++] = line++;
            while(*line != '`'){line++;}
            *++line = '\0';
                line++;
                continue;
        /*
        ** Here would be a good place to check for other special characters
        ** (quotes, '&', etc) and do something clever.
        */

        default:    /* it's just a regular argument word */
            sc->argv[argc++] = line;
            break;
        }
        
        while(*line && !isSeparator(*line))  /* find the end of the argument */
            ++line;
        if(*line == '='){
            sc->env = sc->argv[argc-1];
            *line = '\0';
            sc->argv[argc++] = ++line;
            sc->env_val = sc->argv[argc -1];
            if(*line == '\''){
                line++;
                while(*line && *line != '\'')  /* find the end of the argument */
                    ++line;
                *line = '\0';
                sc->env_val += 1;
            }else if(*line == '"'){
                line++;
                while(*line && *line != '"')  /* find the end of the argument */
                    ++line;
                *line = '\0';
                sc->env_val += 1;
            }{
                while(*line && !isSeparator(*line))  /* find the end of the argument */
                    ++line;
            }

        }
        if(*line == '\0')        /* end-of-line, we're done */
            break;
        else
            *line++ = '\0'; /* terminate the argument and go to next char */
    }

    /* We are at the very end of "line" */
    sc->argv[argc] = NULL;  /* terminate the last command */
    sc->argv[argc + 1] = NULL;  /* marker meaning "no more commands" */

    /*
    ** There's a nasty special case to handle here.  If the command line
    ** contains any commands, then numCommands can simply be set to
    ** (number of '|' symbols + 1).  Note that the only place above
    ** that numCommands is increased is in the "case '|'" of the switch.
    ** So we should add 1 here.  BUT, if there were NO commands (ie, the
    ** command line was empty), then we should NOT add 1.
    */
    if(argc > 0)
        ++sc->numCommands;

    return 0;
}
