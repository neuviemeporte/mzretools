#include "dos/signature.h"
#include "dos/executable.h"
#include "dos/codemap.h"
#include "dos/output.h"
#include "dos/util.h"
#include "dos/error.h"

#include <fstream>

using namespace std;

OUTPUT_CONF(LOG_ANALYSIS)

SignatureLibrary::SignatureLibrary(const CodeMap &map, const Executable &exe, const Size minInstructions) {
    for (Size idx = 0; idx < map.routineCount(); ++idx) {
        const Routine routine = map.getRoutine(idx);
        // TODO: external also, maybe enable with switch
        if (routine.ignore) {
            debug("Ignoring routine: " + routine.dump(false));
            continue;
        }
        debug("Processing routine: " + routine.dump(false));
        // TODO: try other reachable blocks?
        const Block block = routine.mainBlock();
        // extract string of signatures for reference routine
        vector<Signature> sig = exe.getSignatures(block);
        const Size sigSize = sig.size();
        if (sigSize > 0 && sigSize >= minInstructions) {
            verbose("Extracted signature for routine " + routine.name + ", " + to_string(sig.size()) + " instructions");
            sigs.emplace_back(SignatureItem{routine.name, std::move(sig)});
        }
        else debug("Routine too small: " + to_string(sigSize) + " instructions");
    }
}

SignatureLibrary::SignatureLibrary(const std::string &path) {

}

void SignatureLibrary::save(const std::string &path) const {
    if (sigs.empty()) return;
    if (path.empty()) throw ArgError("Empty path for saving signature library");
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