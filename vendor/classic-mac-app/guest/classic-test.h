#ifndef CLASSIC_TEST_H
#define CLASSIC_TEST_H

#include <stdio.h>

typedef struct ClassicTestReport {
    FILE *file;
    unsigned planned;
    unsigned completed;
    unsigned failed;
} ClassicTestReport;

int ClassicTestBegin(ClassicTestReport *report, unsigned planned);
void ClassicTestResult(ClassicTestReport *report, int passed, const char *name,
                       const char *diagnostic);
int ClassicTestEnd(ClassicTestReport *report);

#endif
