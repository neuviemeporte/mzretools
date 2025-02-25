#include "dos/signature.h"
#include "dos/executable.h"
#include "dos/instruction.h"
#include "dos/codemap.h"
#include "dos/output.h"
#include "dos/util.h"
#include "dos/error.h"

#include <iostream>
#include <fstream>

using namespace std;

#ifdef DEBUG
#define PARSE_DEBUG(msg) debug(msg)
#else
#define PARSE_DEBUG(msg)
#endif

OUTPUT_CONF(LOG_ANALYSIS)

SignatureLibrary::SignatureLibrary(const CodeMap &map, const Executable &exe, const Size minInstructions, const Size maxInstructions) {
    for (Size idx = 0; idx < map.routineCount(); ++idx) {
        const Routine routine = map.getRoutine(idx);
        // TODO: external also, maybe enable with switch
        if (routine.ignore || routine.external) {
            debug("Ignoring routine: " + routine.dump(false));
            continue;
        }
        debug("Processing routine: " + routine.dump(false));
        // TODO: try other reachable blocks?
        const Block block = routine.mainBlock();
        // extract string of signatures for reference routine
        SignatureString sig = exe.getSignatures(block);
        const Size sigSize = sig.size();
        if (sigSize == 0) debug("Empty signature, ignoring");
        else if (sigSize < minInstructions) debug("Routine too small: " + to_string(sigSize) + " instructions");
        else if (maxInstructions != 0 && sigSize > maxInstructions) debug("Routine too big: " + to_string(sigSize) + " instructions");
        else {
            verbose("Extracted signature for routine " + routine.name + ", " + to_string(sig.size()) + " instructions");
            sigs.emplace_back(SignatureItem{routine.name, std::move(sig)});
        }
    }
    verbose("Loaded signatures for " + to_string(sigs.size()) + " routines from executable");
}

SignatureLibrary::SignatureLibrary(const std::string &path) {
    if (path.empty()) throw ArgError("Empty path for loading signature library");
    ifstream file{path};
    string line;
    Size lineno = 0;
    while (safeGetline(file, line)) {
        lineno++;
        // ignore comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        string routineName, sigStr;
        SignatureString tmpSigs;
        auto prevPos = line.begin(), curPos = line.begin();
        for (; curPos != line.end(); ++curPos) {
            switch (*curPos) {
            case ':':
                if (!routineName.empty()) throw ParseError("More than one routine name for signature on line " + to_string(lineno));
                routineName = {prevPos, curPos};
                PARSE_DEBUG("Line " + to_string(lineno) + ", found routine name: '" + routineName + "'");
                prevPos = curPos + 1;
                break;
            case ',':
                sigStr = {prevPos, curPos};
                PARSE_DEBUG("Line " + to_string(lineno) + ", found signature token: '" + sigStr + "'");
                prevPos = curPos + 1;
                tmpSigs.emplace_back(stoi(sigStr, nullptr, 16));
                break;
            }
        }
        if (routineName.empty()) throw ParseError("Routine missing on signature file line " + to_string(lineno));
        if (sigStr.empty()) throw ParseError("Signature string missing on signature file line " + to_string(lineno));
        // read final token which is not comma-terminated
        sigStr = {prevPos, curPos};
        PARSE_DEBUG("Line " + to_string(lineno) + ", final signature token: '" + sigStr + "'");
        tmpSigs.emplace_back(stoi(sigStr, nullptr, 16));
        verbose("Loaded signature for routine " + routineName + ", " + to_string(tmpSigs.size()) + " instructions");
        sigs.emplace_back(SignatureItem{routineName, std::move(tmpSigs)});
    }
    verbose("Loaded signatures for " + to_string(sigs.size()) + " routines from " + path);
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

void SignatureLibrary::dump() const {
    const auto dumpOp = [](const OperandType ot, const InstructionPrefix p) {
        if (operandIsMem(ot)) {
            if (prefixIsSegment(p)) cout << prefixName(p);
            cout << "[" << operandName(ot);
            if (operandIsMemWithOffset(ot)) {
                if (!operandIsMemImmediate(ot)) cout << "+";
                if (operandIsMemWithByteOffset(ot)) cout << "off8";
                else if (operandIsMemWithWordOffset(ot)) cout << "off16";
            }
            cout << "]";
        }
        else cout << operandName(ot);
    };
    for (Size i = 0; i < signatureCount(); ++i) {
        const SignatureItem &si = getSignature(i);  
        cout << si.routineName << ": " << si.size() << " instructions" << endl;
        for (const Signature s : si.signature) {
            const InstructionPrefix p = static_cast<InstructionPrefix>((s >> 19) & 0b111);
            const InstructionClass c = static_cast<InstructionClass>((s >> 12) & 0b1111111);
            const OperandType
                op1 = static_cast<OperandType>((s >> 6) & 0b111111),
                op2 = static_cast<OperandType>(s & 0b111111);
            cout << "\t";
            if (prefixIsChain(p)) cout << prefixName(p) << " ";
            cout << instructionName(c);
            if (op1 > OPR_NONE) {
                cout << " ";
                dumpOp(op1, p);
            }
            if (op2 > OPR_NONE) {
                cout << ", ";
                dumpOp(op2, p);
            }
            cout << endl;
        }
    }
}
