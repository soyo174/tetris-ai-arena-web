#import <stdio.h>
#import <string.h>

int main(void) {
    /* The referee writes the decimal request ID before the observation. */
    char line[8192];
    while (fgets(line, sizeof line, stdin)) {
        if (!strchr(line, '\n') && !feof(stdin)) {
            int ch;
            do {
                ch = getchar();
            } while (ch != '\n' && ch != EOF);
        }
        const char *field = strstr(line, "\"request_id\"");
        if (!field) continue;
        field += strlen("\"request_id\"");
        field += strspn(field, " \t");
        if (*field++ != ':') continue;
        field += strspn(field, " \t");
        if (*field++ != '"') continue;
        int length = (int)strspn(field, "0123456789");
        if (!length || field[length] != '"') continue;
        printf("{\"type\":\"PLAY\",\"request_id\":\"%.*s\",\"commands\":[\"HARD_DROP\"]}\n", length, field);
        fflush(stdout);
    }
    return 0;
}
