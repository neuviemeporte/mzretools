#include "dos/signature.h"
#include "dos/executable.h"
#include "dos/codemap.h"
#include "dos/output.h"
#include "dos/util.h"

#include <fstream>

using namespace std;

OUTPUT_CONF(LOG_ANALYSIS)

SignatureLibrary::SignatureLibrary(const CodeMap &map, const Executable &exe) {
    for (Size idx = 0; idx < map.routineCount(); ++idx) {
        const Routine routine = map.getRoutine(idx);
        debug("Processing routine: " + routine.dump(false));
        // TODO: try other reachable blocks?
        const Block block = routine.mainBlock();
        // extract string of signatures for reference routine
        vector<Signature> sig = exe.getSignatures(block);
        if (sig.size() > 0) {
            verbose("Extracted signature for routine " + routine.name + ", " + to_string(sig.size()) + " instructions");
            sigs.emplace_back(SignatureItem{routine.name, std::move(sig)});
        }
    }
}

SignatureLibrary::SignatureLibrary(const std::string &path) {

}

void SignatureLibrary::save(const std::string &path) const {
    if (sigs.empty()) return;
    ofstream file{path};
    for (Size i = 0; i < signatureCount(); ++i) {
        const SignatureItem &si = getSignature(i);
        const Size size = si.signature.size();
        if (size == 0) continue;
        file << si.routineName << ": ";
        for (Size j = 0; j < size; ++j) {
            if (j != 0) file << ",";
            file << hexVal(si.signature[j], false, false);
        }
        file << endl;
    }
}