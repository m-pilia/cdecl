/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Martino Pilia, 2015
 */

/*!
 * \file cdecl.c
 * @author Martino Pilia
 * @date 2015/02/21
 *
 * This simple program is the implementation of a C declaration parser
 * suggested at the end of Chapter 5 in Kernighan and Ritchie's 
 * The C Programming Language, with some addings for C99 declarations and
 * more care for syntax errors.
 *
 * The declarations are formalized in a simplified way, with a recursive
 * grammar composed by:
 *  - fdecl: a complete declarator (maybe a function, a variable etc.)
 *  - fdecll: a comma separated list of fdecl
 *  - decl: a declarator with no returning type (maybe a pointer)
 *  - ddecl: a direct declarator, with no returning type and no pointers
 *
 * A BNF description of the grammar is the following:
 * \code{.bnf}
 *     fdecl  ::= return_type decl
 *
 *     fdecll ::= fdecl [fdecll]
 *
 *     decl   ::= ["*"] decl |
 *                [qualifier] decl | 
 *                ddecl
 *
 *     ddecl  ::= "(" decl ")" |
 *                ddecl "()" |
 *                ddecl "[" [size] "]" |
 *                identifier ddecl |
 *                ddecl "(" fdecll ")"
 * \endcode
 *
 * The recognition of these four nonterminal symbols is implemented in the
 * four homonymous functions.
 *
 * Other facts are considered to detect syntax errors:
 *  - a parentheses opened before any identifier must be for gouping
 *     + and it cannot be followed by a type name
 *  - a parentheses opened after an identifier must be for function delclaration
 *     + and it must be void or followed by a type name
 *  - a square bracket cannot appear if there's no identifier before
 *  - a function cannot return an array nor a function
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>

#include "cdecl.h"

#define TOKEN_LEN 100
#define SPEC_NO 9
#define QUAL_NO 2
#define STO_NO 5
#define SPEC_FOR_TYPE 4

enum {NOTHING, NAME, TYPE, PARENS, POINTER}; /* kind of symbol */

/* private functions */
int is_specifier(char *token);
int is_qualifier(char *token);
int is_storage_class(char *token);
int is_reserved(char *token);
int is_int_literal(char *token);
void get_type(char *line, char *res);
void get_bracket_content(char *line, char *qualifier, int *stat, char *length);
int valid_pair(char *spec1, char *spec2);
char* next_token(char *line, char *token);
void unnext_token(char *token);
void check_declaration(char *out);
void ddecl(char *line, char *out);
void decl(char *line, char *out);
void fdecll(char *line, char *out);
void fdecl(char *line, char *res);

char specifiers[SPEC_NO][TOKEN_LEN] =
{
    "void",
    "char",
    "short",
    "int",
    "long",
    "float",
    "double",
    "signed",
    "unsigned"
};

char qualifiers[QUAL_NO][TOKEN_LEN] =
{
    "const",
    "volatile"
    /* "restrict" is omitted because it applies only to pointers, it is
     * recognized directly inside the decl function */
};

char storage_classes[STO_NO][TOKEN_LEN] =
{
    "auto",
    "register",
    "static",
    "extern",
    "typedef"
};

char token_buf[TOKEN_LEN]; /* token buffer */
int is_token_buf = 0; /* nonzero when a token is in the buffer */
int last = NOTHING;   /* last kind of symbol read */
int fun_nest_lev = 0; /* how many function parentheses are currently opene */
jmp_buf jump_buffer;
char error_msg[200] = "";

char storage_class[TOKEN_LEN] = ""; /* value for the storage class */
int name_found = 0; /* nonzero if an identifier has been found yet */

/*!
 * \brief Determine if the input token is a type specifier.
 * @param token Input token.
 */
int is_specifier(char *token)
{
    int i;
    for (i = 0; i < SPEC_NO; ++i)
        if (!strcmp(token, specifiers[i]))
            return 1;
    return 0;
}

/*!
 * \brief Determine if the input token is a type qualifier. 
 * @param token Input token.
 */
int is_qualifier(char *token)
{
    int i;
    for (i = 0; i < QUAL_NO; ++i)
        if (!strcmp(token, qualifiers[i]))
            return 1;
    return 0;
}

/*!
 * \brief Determine if the input token is a storage class.
 * @param token Input token.
 */
int is_storage_class(char *token)
{
    int i;
    for (i = 0; i < STO_NO; ++i)
        if (!strcmp(token, storage_classes[i]))
            return 1;
    return 0;
}

/*!
 * \brief Determine if a token is a type name or qualifier of a storage class
 * @param token Input token.
 */
int is_reserved(char *token)
{
    return 
        is_specifier(token) || 
        is_qualifier(token) || 
        is_storage_class(token);
}

/*!
 * \brief Determine if a token is a valid integer literal.
 * @param token Input token.
 */
int is_int_literal(char *token)
{
    int i, j, len = strlen(token), l_num = 0, u_num = 0;
    char c;

    /* first char must be digit */
    if (!isdigit(token[0]))
        return 0;

    /* second char may be digit, \0, x or X or b (if first is 0), 
     * u or l (upper or lower) if len < 4 */
    c = tolower(token[1]);
    if (
            !isdigit(token[1]) &&
            token[1] != '\0' &&
            c != 'x' && 
            c != 'b' &&
            (c == 'l' && len > 4) &&
            (c == 'u' && len > 4)
            )
        return 0;
    if (
            (c == 'x' || c == 'b')
            && (token[0] != '0' || len < 3))
        return 0;

    /* last chars may be a suffix of 1 or 2 l or 1 u (upper or lower) */
    j = len; /* count suffix length */
    while (--j >= len - 3)
    {
        c = tolower(token[j]);

        if (isdigit(token[j]) || (tolower(token[1]) == 'x' && isxdigit(c)))
            break; /* found digit */

        if (c == 'l')
        {
            if (++l_num > 2) /* suffix cannot contain more than 2 l */
                return 0;
            continue;
        }

        if (c == 'u')
        {
            if (++u_num > 1) /* suffix cannot contain more than 1 u */
                return 0;
            continue;
        }

        return 0; /* found other char than l or u (or hex digit when allowed) */
    }

    /* other chars must be digits */
    if (token[0] == '0')
    {
        c = tolower(token[1]);

        /* hex */
        if (c == 'x')
        {
            for (i = 2; i <= j; ++i)
                if (!isxdigit(token[i]))
                    return 0;
        }

        /* bin */
        else if (c == 'b')
        {
            for (i = 2; i <= j; ++i)
                if (token[i] < '0' || token[i] > '1')
                    return 0;
        }

        /* oct */
        else
        {
            for (i = 1; i <= j; ++i)
                if (token[i] < '0' || token[i] > '7')
                    return 0;
        }
    }
    else
    {
        /* decimal */
        for (i = 0; i <= j; ++i)
            if (!isdigit(token[i]))
                return 0;
    }

    return 1;
}

/*!
 * \brief Parse a type (without storage class) from the current input position.
 */
void get_type(char *line, char *res)
{
    char token[TOKEN_LEN];
    char specifier[SPEC_FOR_TYPE][TOKEN_LEN] = {{0}};
    char qualifier[TOKEN_LEN] = "";
    int long_counter = 0;
    int i, j;

    while (next_token(line, token) && is_reserved(token))
    {
        if (is_specifier(token))
        {
            for (i = 0; i < SPEC_FOR_TYPE; i++)
            {
                if (!strcmp(specifier[i], ""))
                {
                    strcpy(specifier[i], token);
                    break;
                }
            }
            if (i == SPEC_FOR_TYPE && strcmp(specifier[SPEC_FOR_TYPE - 1], ""))
            {
                sprintf(error_msg, "syntax error: too many specifiers\n");
                longjmp(jump_buffer, 1);
            }
        }
        else if (is_qualifier(token))
        {
            /* first qualifier */
            if (!strcmp(qualifier, ""))
                strcpy(qualifier, token);

            /* if different qualifiers are providen */
            else if (strcmp(qualifier, token))
            {
                sprintf(error_msg, "syntax error: %s incompatible with "
                        "previous qualifier %s\n", token, qualifier);
                longjmp(jump_buffer, 1);
            }
            /* otherwise, duplicate qualifier is ignored (C99) */
        }
        else if (is_storage_class(token))
        {
            if (!strcmp(storage_class, ""))
                strcpy(storage_class, token);
            else
            {
                sprintf(error_msg, "syntax error: unexpected storage class\n");
                longjmp(jump_buffer, 1);
            }
        }
    }

    if (!strcmp(token, "")) /* EOL */
        return;

    unnext_token(token); /* put back unused token */

    /* if there is no specifier, set to default (int) */
    if (!strcmp(specifier[0], ""))
        strcpy(specifier[0], "int");

    /* check whether specifier combination is legal */
    for (i = 0; i < SPEC_FOR_TYPE; ++i)
        for (j = i + 1; j < SPEC_FOR_TYPE; ++j)
            valid_pair(specifier[i], specifier[j]);

    /* ensure there are no more than 2 long specifiers (1 with double) */
    for (i = 0; i < SPEC_FOR_TYPE; ++i)
        if (!strcmp(specifier[i], "long") || !strcmp(specifier[i], "double"))
            long_counter++;
    if (long_counter > 2)
    {
        sprintf(error_msg, "syntax error: too many \"long\" specifiers");
        longjmp(jump_buffer, 1);
    }

    /* write result in the res string */
    strcpy(res, "");
    if (strcmp(qualifier, ""))
    {
        strcat(res, qualifier);
        strcat(res, " ");
    }
    strcat(res, specifier[0]);
    i = 1;
    while (strcmp(specifier[i], ""))
    {
        strcat(res, " ");
        strcat(res, specifier[i++]);
    }
}

/*!
 * \brief Get the content of square brackets, esuring it is a valid integer 
 * literal.
 *
 * Parameters must be initialized to 0 by the caller.
 * The function is recursive, and the last found qualifier is memorized (as
 * static variable, because the value must be shared amond different recursive
 * calls) to ensure two qualifiers are not in conflict.
 *
 * @param line Input line.
 * @param qualifier Value found for the qualifier
 * @param stat Value found for "static" array size (C99).
 * @param length Value found for the array length.
 */
void get_bracket_content(char *line, char *qualifier, int *stat, char *length)
{
    static char last_qualifier[TOKEN_LEN] = ""; /* memorize last qualifier */
    char token[TOKEN_LEN];

    /* open bracket [ cannot be the last token */
    if (!next_token(line, token))
    {
        sprintf(error_msg, "syntax error: unbalanced brackets\n");
        longjmp(jump_buffer, 1);
    }

    /* check if the next token is a qualifier (C99) */
    if (
            is_qualifier(token) || 
            !strcmp(token, "restricted"))
    {
        /* this stuff is valid only in function parameters */
        if (!fun_nest_lev)
        {
            sprintf(error_msg, "syntax error: static or type qualifiers in "
                    "non-parameter array declarator\n");
            longjmp(jump_buffer, 1);
        }
        if (!strcmp(last_qualifier, "") || !strcmp(last_qualifier, token))
        {
            strcpy(qualifier, token);
            strcpy(last_qualifier, token);
        }
        else
        {
            sprintf(error_msg, "syntax error: %s incompatible with "
                    "previous qualifier %s\n", token, qualifier);
            longjmp(jump_buffer, 1);
        }

        /* recursive call */
        get_bracket_content(line, qualifier, stat, length);
    }
    /* check if the array has static attribute (C99) */
    else if (!strcmp(token, "static"))
    {
        /* this stuff is valid only in function parameters */
        if (!fun_nest_lev)
        {
            sprintf(error_msg, "syntax error: static or type qualifiers in "
                    "non-parameter array declarator\n");
            longjmp(jump_buffer, 1);
        }

        *stat = 1;

        /* recursive call */
        get_bracket_content(line, qualifier, stat, length);
    }
    /* check if the next token is (not) the array length */
    else if (!is_int_literal(token))
    {
        /* if next token is not the length nor closing bracket */
        if (strcmp(token, "]"))
        {
            sprintf(error_msg, 
                    "syntax error: invalid value %s, array size must be "
                    "positive int\n", token);
            longjmp(jump_buffer, 1);
        }
    }
    else
    {
        strcat(length, token); /* write array length */

        /* if previous token was array lenght, check next is 
         * closing bracket */
        if (!next_token(line, token) || strcmp(token, "]"))
        {
            sprintf(error_msg, "syntax error: unbalanced brackets\n");
            longjmp(jump_buffer, 1);
        }
    }

    strcpy(last_qualifier, ""); /* clear static variable for next call */
}

/*! 
 * \brief Check whether a pair of specifiers is legal in the same declaration,
 * according with the following table:
 *
 * <pre>
 *           void  char  short  int  long  float  double  signed  unsigned
 * void      n     n     n      n    n     n      n       n       n
 * char      n     n     n      n    n     n      n       YES     YES
 * short     n     n     n      YES  n     n      n       YES     YES
 * int       n     n     YES    n    YES   n      n       YES     YES
 * long      n     n     n      YES  YES   n      n       YES     YES
 * float     n     n     n      n    n     n      n       YES     YES
 * double    n     n     n      n    YES   n      n       YES     YES
 * signed    n     YES   YES    YES  YES   YES    YES     n       n
 * unsigned  n     YES   YES    YES  YES   YES    YES     n       n
 * </pre>
 */
int valid_pair(char *spec1, char *spec2)
{
    if (
            (!strcmp(spec1, "void") && strcmp(spec2, "")) ||
            (!strcmp(spec1, "char") && (
                                        strcmp(spec2, "unsigned") &&
                                        strcmp(spec2, "signed") &&
                                        strcmp(spec2, ""))
                                        ) ||
            (!strcmp(spec1, "short") && (
                                        strcmp(spec2, "int") &&
                                        strcmp(spec2, "unsigned") &&
                                        strcmp(spec2, "signed") &&
                                        strcmp(spec2, ""))
                                        ) ||
            (!strcmp(spec1, "int") && (
                                        strcmp(spec2, "long") &&
                                        strcmp(spec2, "short") &&
                                        strcmp(spec2, "unsigned") &&
                                        strcmp(spec2, "signed") &&
                                        strcmp(spec2, ""))
                                        ) ||
            (!strcmp(spec1, "float") && (
                                        strcmp(spec2, "unsigned") &&
                                        strcmp(spec2, "signed") &&
                                        strcmp(spec2, ""))
                                        ) ||
            (!strcmp(spec1, "double") && (
                                        strcmp(spec2, "long") &&
                                        strcmp(spec2, "unsigned") &&
                                        strcmp(spec2, "signed") &&
                                        strcmp(spec2, ""))
                                        ) ||
            (!strcmp(spec1, "signed") && (
                                        !strcmp(spec2, "void") ||
                                        !strcmp(spec2, "signed") ||
                                        !strcmp(spec2, "unsigned"))
                                        ) ||
            (!strcmp(spec1, "unsigned") && (
                                        !strcmp(spec2, "void") ||
                                        !strcmp(spec2, "signed") ||
                                        !strcmp(spec2, "unsigned"))
                                        )
       )
        {
            sprintf(error_msg, 
                    "syntax error: specifier %s incompatible with %s\n", 
                    spec2,
                    spec1);
            longjmp(jump_buffer, 1);
        }
    return 1;
}

/*!
 * \brief Get next token from the input line.
 *
 * Each call gives the next token of the line, or an empty string when EOL has 
 * been reached. A copy of the input line is memorized, and when the input 
 * changes the token count is resetted and the scan restarts from the 
 * beginning of the line.
 *
 * @param token Input token
 * @return Pointer to the beginning of the token, NULL if EOL has been reached.
 */
char* next_token(char *line, char *token)
{
    static int beg_token = 0; /* first char of the current token */
    static char prev_line[1000] = ""; /* copy of the last input line */
    int end_token; /* last char of the current token */

    /* reset counter if input line has changed */
    if (strcmp(line, prev_line))
    {
        beg_token = 0;
        strcpy(prev_line, line);
    }

    /* check if there's a token in the buffer, return that if so */
    if (is_token_buf)
    {
        strcpy(token, token_buf);
        is_token_buf = 0;
        return token_buf;
    }

    /* ignore leading spaces and tabs */
    while (line[beg_token++] == ' ' || line[beg_token] == '\t'); 
    end_token = --beg_token;

    if (line[beg_token] == '\0') /* reached EOL */
    {
        strcpy(token, "");
        return NULL;
    }

    /* punctuation token */
    if (
            line[end_token] == '(' || 
            line[end_token] == ')' ||
            line[end_token] == '[' ||
            line[end_token] == ']' ||
            line[end_token] == ',' ||
            line[end_token] == ';' ||
            line[end_token] == '*' )
    {
        token[0] = line[end_token++];
        token[1] = '\0';
    }

    /* name token */
    else if (isalpha(line[beg_token]))
    {
        int i = 0;
        while (isalnum(line[end_token]))
            token[i++] = line[end_token++];
        token[i] = '\0';
    }

    /* literal token */
    else if (isdigit(line[beg_token]))
    {
        int i = 0;
        while (isalnum(line[end_token]) || line[end_token] == '.')
            token[i++] = line[end_token++];
        token[i] = '\0';
    }

    /* anything else */
    else
    {
        sprintf(error_msg, 
                "syntax error: unexpected token %c\n", 
                line[beg_token]);
        longjmp(jump_buffer, 1);
    }
    
    beg_token = end_token;
    return &line[end_token];
}

/*!
 * \brief Push back a token, wich will be returned by the next next_token() call.
 * @param token Token to be pushed back.
 */
void unnext_token(char *token)
{
    if (is_token_buf)
    {
        sprintf(error_msg, "buffer alredy occupied\n");
        longjmp(jump_buffer, 1);
    }
    strcpy(token_buf, token);
    is_token_buf = 1;
}

/*!
 * \brief Check for invalid declarations in the output line.
 */
void check_declaration(char *out)
{
    if (
            strstr(out, "returning array") ||
            strstr(out, "returning static array")
            )
    {
        sprintf(error_msg, "syntax error: cannot return array\n");
        longjmp(jump_buffer, 1);
    }
    if (
            strstr(out, "returning function") 
            )
    {
        sprintf(error_msg, "syntax error: cannot return function\n");
        longjmp(jump_buffer, 1);
    }
    if (
            strstr(out, "] of function") 
            )
    {
        sprintf(error_msg, "syntax error: cannot declare array of functions\n");
        longjmp(jump_buffer, 1);
    }

    if (
            strstr(out, ", void") ||
            strstr(out, "void, ") 
            )
    {
        sprintf(error_msg, "syntax error: void must be the only parameter\n");
        longjmp(jump_buffer, 1);
    }

    if (
            strstr(out, "] of void")
            )
    {
        sprintf(error_msg, "syntax error: cannot declare array of void\n");
        longjmp(jump_buffer, 1);
    }
}

/*!
 * \brief Parse a decl declarator (which has not a return type).
 * @param line Input string
 * @param out Output string
 */
void decl(char *line, char *out)
{
    /* last qualifier in current recursion */
    static char prev_qualifier[TOKEN_LEN] = ""; 

    char token[TOKEN_LEN], qualifier[TOKEN_LEN] = "";
    int pointer = 0; /* nonzero if a pointer declarator is present */
    int restricted = 0; /* nonzero if pointer is restricted */

    /* check if there is a pointer declaration */
    if (next_token(line, token) && !strcmp(token, "*"))
    {
        pointer = 1;
        strcpy(prev_qualifier, ""); /* any qualifier allowed after pointer */
    }
    else
        unnext_token(token);

    /* check for restrict qualifier */
    while (next_token(line, token) && !strcmp(token, "restrict"))
    {
        if (pointer)
            restricted = 1;
        else
        {
            sprintf(error_msg,
                    "syntax error: restrict qualifier applies to pointers "
                    "only\n");
            longjmp(jump_buffer, 1);
        }
    }
    unnext_token(token);

    /* check if there is a qualifier, otherwise give back token to the buffer */
    if (next_token(line, token) && is_qualifier(token))
    {
        /* if there is another neigh different qualifier */
        if (strcmp(prev_qualifier, token) && strcmp(prev_qualifier, ""))
        {
            sprintf(error_msg, 
                    "syntax error: %s incompatible with previous qualifier %s\n",
                    token, prev_qualifier);
            longjmp(jump_buffer, 1);
        }
        
        strcpy(prev_qualifier, token);
        strcpy(qualifier, token);
    }
    else
        unnext_token(token);

    /* recursive call */
    if (pointer || strcmp(qualifier, ""))
        decl(line, out); /* search for another pointer/qualifier */
    else
        ddecl(line, out); /* no pointer nor qualifiers anymore */

    /* write qualifier if present */
    if (strcmp(qualifier, ""))
    {
        strcat(out, qualifier);
        strcat(out, " ");
    }

    /* write restrict if present */
    if (restricted)
    {
        strcat(out, "restrict ");
    }

    /* write pointer if present */
    if (pointer)
        strcat(out, "pointer to ");

    /* reset variable content when recursion is terminated */
    strcpy(prev_qualifier, "");
}

/*! 
 * \brief Parse a comma separated list of fdecl declarators 
 * @param line Input string
 * @param out Output string
 */
void fdecll(char *line, char *out)
{
    char token[TOKEN_LEN];
    int name_found_copy = name_found; 

    /* save value of name_found in the lower level of nesting, then reset
     * it to 0, allowing function parameters to have their names in the
     * upper nesting level */
    name_found = 0;

    if (!next_token(line, token))
    {
        sprintf(error_msg, "syntax error: unexpected end of list");
        longjmp(jump_buffer, 1);
    }

    /* end of list */
    if (!strcmp(token, ")"))
    {
        if (strcmp(storage_class, ""))
        {
            sprintf(error_msg, "syntax error: unexpected storage class\n");
            longjmp(jump_buffer, 1);
        }
        return;
    }

    /* comma */
    else if (!strcmp(token, ","))
        strcat(out, ", ");

    /* other token (must be returned in the buffer) */
    else
        unnext_token(token);

    fdecl(line, out);
    fdecll(line, out);

    /* restore value at exit, returning to lower nesting level */
    name_found = name_found_copy;
}

/*!
 * \brief Parse a direct declarator, i.e. a decl declarator which is not a 
 * pointer.
 * @param line Input string
 * @param out Output string
 */
void ddecl(char *line, char *out)
{
    char token[TOKEN_LEN];

    /* nothing more to parse */
    if (!next_token(line, token))
        return;

    /* pointer or restrict in invalid position */
    if (!strcmp(token, "*") || !strcmp(token, "restrict"))
    {
        sprintf(error_msg, "syntax error: unexpected token %s\n", token);
        longjmp(jump_buffer, 1);
    }

    /* closing parentheses */
    if (!strcmp(token, ")") && (last != NAME && last != TYPE))
    {
        sprintf(error_msg,
                "syntax error: expected identifier or type before "
                "%s token\n",
                token);
        longjmp(jump_buffer, 1);
    }

    /* closing bracket */
    if (!strcmp(token, "]"))
    {
        sprintf(error_msg, "syntax error: unexpected token %s\n", token);
        longjmp(jump_buffer, 1);
    }

    /* found a semicolon */
    if (!strcmp(token, ";"))
        return;

    /* found type name, qualifier or storage class */
    if (is_reserved(token))
    {
        sprintf(error_msg, "syntax error: unexpected token %s\n", token);
        longjmp(jump_buffer, 1);



        char type[200];

        /* found type after a name was found and out of a function declaration */
        if (name_found && last != PARENS)
        {
            sprintf(error_msg, 
                    "syntax error: unexpected identifier %s\n", token);
            longjmp(jump_buffer, 1);
        }

        unnext_token(token);
        get_type(line, type);
        strcat(out, type);
        last = TYPE;
        decl(line, out);
        return;
    }

    /* found a name */
    if (isalpha(token[0]))
    {
        /* don't allow two identifiers inside the same function nesting level */
        if (name_found && last != TYPE)
        {
            sprintf(error_msg, 
                    "syntax error: unexpected identifier %s\n", token);
            longjmp(jump_buffer, 1);
        }
        name_found = 1;
        strcat(out, token); /* write name */
        strcat(out, ": ");

        /* write storage class */
        if (strcmp(storage_class, ""))
        {
            strcat(out, storage_class);
            strcat(out, " ");
            strcpy(storage_class, "");
        }
        last = NAME;

        ddecl(line, out);
        return;
    }

    /* found numeric literal */
    if (isdigit(token[0]))
    {
        sprintf(error_msg, "syntax error: unexpected token %s\n", token);
        longjmp(jump_buffer, 1);
    }

    /* found an open parentheses */
    if (!strcmp(token, "("))
    {
        /* ensure open parentheses is not the last token */
        if (!next_token(line, token))
        {
            sprintf(error_msg, "syntax error: unmatching parentheses\n");
            longjmp(jump_buffer, 1);
        }

        /* found couple () of parentheses */
        if (!strcmp(token, ")"))
        {
            /* cannot declare function [pointer] without a name */
            if (!name_found)
            {
                sprintf(error_msg, "syntax error: expected identifier\n");
                longjmp(jump_buffer, 1);
            }
            strcat(out, "function() returning ");
            ddecl(line, out);
            return;
        }

        last = PARENS;
        
        /* found type name, i.e. function argument (parentheses are part of a
         * function declaration) */
        if (isalpha(token[0]) && is_reserved(token))
        {
            /* cannot declare function [pointer] without a name */
            if (!name_found)
            {
                sprintf(error_msg, "syntax error: expected identifier "
                        "before ( token\n");
                longjmp(jump_buffer, 1);
            }
            unnext_token(token);

            fun_nest_lev++;
            strcat(out, "function (");
            fdecll(line, out);
            strcat(out, ") returning ");
            fun_nest_lev--;

            /* ensure there is no identifier after closing parenteses */
            if (!next_token(line,token))
                return;
            if (isalpha(token[0]))
            {
                sprintf(error_msg, 
                        "syntax error: unexpected identifier %s\n", token);
                longjmp(jump_buffer, 1);
            }
            unnext_token(token);

            ddecl(line, out);
            return;
        }

        /* function argument cannot start with ( or [ */
        if ((!strcmp(token, "(") && name_found) || !strcmp(token, "["))
        {
            sprintf(error_msg, "syntax error: unexpected token %s\n", token);
            longjmp(jump_buffer, 1);
        }

        unnext_token(token); /* put back unused token */

        /* no function declaration, parentheses are for grouping, so search 
         * for a decl */
        decl(line, out);

        /* unbalanced parentheses */
        if (!next_token(line, token) || strcmp(token, ")"))
        {
            sprintf(error_msg, "syntax error: unbalanced parentheses\n");
            longjmp(jump_buffer, 1);
        }

        /* ensure there is no identifier after closing parenteses */
        if (!next_token(line,token))
            return;
        if (strcmp(token, "(") && strcmp(token, ")") && strcmp(token, "["))
        {
            sprintf(error_msg,
                    "syntax error: unexpected identifier %s\n", token);
            longjmp(jump_buffer, 1);
        }
        unnext_token(token);

        ddecl(line, out);
        return;
    }

    /* found an open bracket [ */
    if (!strcmp(token, "["))
    {
        char length[TOKEN_LEN] = "";
        char qualifier[TOKEN_LEN] = "";
        int is_static = 0;

        /* brackets cannot precede an identifier */
        if (!name_found && !fun_nest_lev)
        {
            sprintf(error_msg, 
                    "syntax error: expected identifier before [ token\n");
            longjmp(jump_buffer, 1);
        }

        get_bracket_content(line, qualifier, &is_static, length);

        /* ensure length is present if array size is declared static */
        if (is_static && !strcmp(length, ""))
        {
            sprintf(error_msg,
                    "syntax error: expected array length after static\n");
            longjmp(jump_buffer, 1);
        }

        /* write qualifier and size */
        if (strcmp(qualifier, ""))
        {
            strcat(out, qualifier);
            strcat(out, " ");
        }
        strcat(out, "array[");
        if (is_static)
            strcat(out, "at least ");
        strcat(out, length);
        strcat(out, "] of ");

        ddecl(line, out);
        return;
    }

    unnext_token(token);
}

/*!
 * Parse a declarator with a type, i.e. variable or [pointer to] function
 * declarator.
 * @param line Input string
 * @param out Output string
 */
void fdecl(char *line, char *out)
{
    char type[200], token[TOKEN_LEN];

    get_type(line, type); /* get return type */
    last = TYPE;

    if (!strcmp(type, ""))
    {
        sprintf(error_msg, "syntax error: expected type\n");
        longjmp(jump_buffer, 1);
    }

    if (!next_token(line, token) || !strcmp(token, ";"))
    {
        sprintf(error_msg, "syntax error: missing object\n");
        longjmp(jump_buffer, 1);
    }
    unnext_token(token);

    decl(line, out);

    strcat(out, type); /* write return type */

    check_declaration(out); /* check for invalid declarations */
}

/*!
 * \brief The actual converter.
 * @param line Input string
 * @param out Output string
 */
void cdecl(char *line, char *out)
{
    strcpy(out, "");
    strcpy(storage_class, "");
    strcpy(error_msg, "");
    name_found = 0;
    last = NOTHING;
    is_token_buf = 0;
    fun_nest_lev = 0;
    

    if (!setjmp(jump_buffer))
        fdecl(line, out);
    else
        strcpy(out, error_msg);
}
