#ifndef SIGNATURE_H
#define SIGNATURE_H

#include "dos/types.h"
#include "dos/address.h"

#include <vector>
#include <string>

using SignatureString = std::vector<Signature>;
class CodeMap;
class Executable;

struct SignatureItem {
    std::string routineName;
    Block routineExtents;
    SignatureString signature;
    SignatureItem(const std::string &routineName, const Block &routineExtents, SignatureString &&signature) : 
        routineName(routineName), routineExtents(routineExtents), signature(signature) {}
    Size size() const { return signature.size(); }
};

class SignatureLibrary {
    std::vector<SignatureItem> sigs;
public:
    SignatureLibrary(const CodeMap &map, const Executable &exe, const Size minInstructions, const Size maxInstructions = 0);
    SignatureLibrary(const std::string &path);
    bool empty() const { return sigs.empty(); }
    Size signatureCount() const { return sigs.size(); }
    const SignatureItem& getSignature(const Size idx) const { return sigs[idx]; }
    void save(const std::string &path) const;
    void dump() const;
};

#endif // SIGNATURE_H