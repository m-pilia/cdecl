#include <stdio.h>
#include <string.h>
#include "cdecl.h"

int main(void)
{
    char in[1000], out[1000];
    char c;
    while (1)
    {
        strcpy(in, "");
        scanf("%[^\n]s", in);
        while ((c = getchar()) != EOF && c != '\n') {} /* clear stdin */

        /* quit if input string is empty */
        if (!strcmp(in, ""))
            return 0;

        /* parse */
        cdecl(in, out);

        /* print result */
        printf("%s\n\n", out);
    }

    return 0;
}
