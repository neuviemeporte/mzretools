#include "gtest/gtest.h"
#include "dos/output.h"
#include "debug.h"

#include <string>

using namespace std;
DebugStream debug_stream;

int main(int argc, char* argv[]) {
    TRACE_ENABLE(false);
    setOutputLevel(LOG_SILENT);    
    for (int i = 0; i < argc; ++i) {
        const string arg{argv[i]};
        if (arg == "--debug") {
            TRACE_ENABLE(true);
            setOutputLevel(LOG_DEBUG);
        }
        else if (arg == "--noanal") {
            setModuleVisibility(LOG_ANALYSIS, false);
        }
        else if (arg == "--nocpu") {
            setModuleVisibility(LOG_CPU, false);
        }        
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
