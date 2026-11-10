#include "classic-test.h"

static void WriteDiagnostic(FILE *file, const char *diagnostic)
{
    if(diagnostic == NULL || *diagnostic == '\0')
        return;
    fputs("# ", file);
    while(*diagnostic != '\0') {
        char character = *diagnostic++;
        fputc(character, file);
        if(character == '\n' && *diagnostic != '\0')
            fputs("# ", file);
    }
    if(diagnostic[-1] != '\n')
        fputc('\n', file);
}

int ClassicTestBegin(ClassicTestReport *report, unsigned planned)
{
    report->file = fopen("out", "w");
    report->planned = planned;
    report->completed = 0;
    report->failed = 0;
    if(report->file == NULL)
        return 0;
    fprintf(report->file, "TAP version 13\n1..%u\n", planned);
    return 1;
}

void ClassicTestResult(ClassicTestReport *report, int passed, const char *name,
                       const char *diagnostic)
{
    if(report->file == NULL)
        return;
    ++report->completed;
    fprintf(report->file, "%s %u - %s\n", passed ? "ok" : "not ok",
            report->completed, name == NULL ? "guest test" : name);
    if(!passed) {
        ++report->failed;
        WriteDiagnostic(report->file, diagnostic);
    }
}

int ClassicTestEnd(ClassicTestReport *report)
{
    int failed = report->file == NULL || report->failed != 0 ||
                 report->completed != report->planned;
    if(report->file != NULL) {
        if(report->completed != report->planned)
            fprintf(report->file, "# planned %u tests but completed %u\n",
                    report->planned, report->completed);
        if(fclose(report->file) != 0)
            failed = 1;
        report->file = NULL;
    }
    return failed ? 1 : 0;
}
