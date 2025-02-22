#include <sage3basic.h>
#include <Combinatorics.h>
#include <integerOps.h>

namespace Rose {
namespace Combinatorics {

ROSE_DLL_API bool
flip_coin()
{
    return Sawyer::fastRandomIndex(2) == 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hasher base class
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string
Hasher::toString(const Digest &digest) {
    std::string str;
    for (size_t i=0; i<digest.size(); ++i) {
        str += "0123456789abcdef"[(digest[i] >> 4) & 0xf];
        str += "0123456789abcdef"[digest[i] & 0xf];
    }
    return str;
}

std::string
Hasher::toString() {
    return toString(digest());
}

void
Hasher::print(std::ostream &out) {
    out <<toString(digest());
}

boost::shared_ptr<Hasher> Hasher::createHasher(const std::string& inType) 
{
    std::string type;
    std::transform(inType.begin(), inType.end(), back_inserter(type), ::toupper);
    
    if(type == "SHA256") {
        HasherSha256Builtin* hasher = new HasherSha256Builtin;
        boost::shared_ptr<Hasher> hashPtr(hasher);
        return hashPtr;
    }
    if(type == "FNV") {
        HasherFnv* hasher = new HasherFnv;
        boost::shared_ptr<Hasher> hashPtr(hasher);
        return hashPtr;
    }
#ifdef ROSE_HAVE_LIBGCRYPT
    if(type == "MD5" || type == "GCRYPT_MD5") {
        HasherMd5* hasher = new HasherMd5;
        boost::shared_ptr<Hasher> hashPtr(hasher);
        return hashPtr;
    }
    if(type == "SHA1" || type == "GCRYPT_SHA1") {
        HasherSha1* hasher = new HasherSha1;
        boost::shared_ptr<Hasher> hashPtr(hasher);
        return hashPtr;
    }
    if(type == "GCRYPT_SHA256") {
        HasherSha256* hasher = new HasherSha256;
        boost::shared_ptr<Hasher> hashPtr(hasher);
        return hashPtr;
    }
    if(type == "SHA384" || type == "GCRYPT_SHA384") {
        HasherSha384* hasher = new HasherSha384;
        boost::shared_ptr<Hasher> hashPtr(hasher);
        return hashPtr;
    }
    if(type == "SHA512" || type == "GCRYPT_SHA512") {
        HasherSha512* hasher = new HasherSha512;
        boost::shared_ptr<Hasher> hashPtr(hasher);
        return hashPtr;
    }
    if(type == "CRC_32"  || type == "GCRYPT_CRC32") {
        HasherCrc32* hasher = new HasherCrc32;
        boost::shared_ptr<Hasher> hashPtr(hasher);
        return hashPtr;
    }


#endif
    return boost::shared_ptr<Hasher>();
    
}

ROSE_DLL_API std::string digest_to_string(const uint8_t *data, size_t size) {
    std::vector<uint8_t> digest(data+0, data+size);
    return Hasher::toString(digest);
}

ROSE_DLL_API std::string
digest_to_string(const std::vector<uint8_t> &data) {
    return Hasher::toString(data);
}

ROSE_DLL_API std::string
digest_to_string(const std::string &data) {
    const char *s = data.c_str();
    std::vector<uint8_t> digest(s, s + data.size());
    return Hasher::toString(digest);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SHA1 hashing
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ROSE_DLL_API std::vector<uint8_t>
sha1_digest(const std::vector<uint8_t> &data) {
    HasherSha1 hasher;
    if (!data.empty())
        hasher.append(&data[0], data.size());
    return hasher.digest();
}

ROSE_DLL_API std::vector<uint8_t>
sha1_digest(const std::string &data) {
    HasherSha1 hasher;
    hasher.insert(data);
    return hasher.digest();
}

ROSE_DLL_API std::vector<uint8_t>
sha1_digest(const uint8_t *data, size_t size) {
    HasherSha1 hasher;
    hasher.append(data, size);
    return hasher.digest();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fowler-Noll-Vo hashing
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const Hasher::Digest&
HasherFnv::digest() {
    if (digest_.empty()) {
        ASSERT_require(8 == sizeof partial_);
        digest_.resize(8, 0);
        for (size_t i=0; i<8; ++i)
            digest_[i] = (partial_ >> ((7-i)*8)) & 0xff;
    }
    return Hasher::digest();
}

void
HasherFnv::append(const uint8_t *message, size_t messageSize) {
    ASSERT_require(message || 0==messageSize);
    if (!digest_.empty())
        throw Exception("cannot append after returning digest");
    if (messageSize > 0)
        for (size_t i=0; i<messageSize; ++i)
            partial_ = (partial_ ^ message[i]) * 0x100000001b3ull;
}

ROSE_DLL_API uint64_t
fnv1a64_digest(const std::vector<uint8_t> &data) {
    HasherFnv hasher;
    if (!data.empty())
        hasher.append(&data[0], data.size());
    return hasher.partial();
}

ROSE_DLL_API uint64_t
fnv1a64_digest(const std::string &data) {
    HasherFnv hasher;
    hasher.insert(data);
    return hasher.partial();
}

ROSE_DLL_API uint64_t
fnv1a64_digest(const uint8_t *data, size_t size) {
    HasherFnv hasher;
    hasher.append(data, size);
    return hasher.partial();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Built-in SHA-256
// Implementation based on FIPS PUB 180-4 "Federal Information Processing Standards Publication: Secure Hash Standard (SHS)"
// found at [https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf]
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The first 32 bits of the fractional parts of the cube roots of the first 64 primes
const uint32_t HasherSha256Builtin::roundConstants_[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

HasherSha256Builtin::HasherSha256Builtin() {
    clear();
}

void
HasherSha256Builtin::clear() {
    Hasher::clear();
    processedBytes_ = 0;
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
}

uint8_t
HasherSha256Builtin::messageByte(size_t index, const uint8_t *message, size_t messageSize) {
    if (index < leftoverBytes_.size())
        return leftoverBytes_[index];
    index -= leftoverBytes_.size();
    ASSERT_require(index < messageSize);
    return message[index];
}

// Get the next 16 words from the big-endian message if possible, or save the message for later. Update message pointer and
// size to reflect the number of bytes we just consumed or saved.
bool
HasherSha256Builtin::getNextChunk(const uint8_t *&message /*in,out*/, size_t &messageSize /*in,out*/, uint32_t words[16] /*out*/) {
    ASSERT_require(leftoverBytes_.size() < 16 * 4);
    ASSERT_require(message != NULL || 0 == messageSize);

    if (leftoverBytes_.size() + messageSize >= 16 * 4) {
        // Obtain the words if the previously saved data plus the current message is long enough
        for (size_t i=0; i<16; ++i) {
            uint32_t word = (messageByte(i*4+0, message, messageSize) << 24) |
                            (messageByte(i*4+1, message, messageSize) << 16) |
                            (messageByte(i*4+2, message, messageSize) <<  8) |
                            (messageByte(i*4+3, message, messageSize)      );
            words[i] = word;
        }

        size_t toConsume = 16 * 4 - leftoverBytes_.size();
        message += toConsume;
        messageSize -= toConsume;
        leftoverBytes_.clear();
        return true;

    } else {
        // Still not enough data to create 16 32-bit words
        leftoverBytes_.insert(leftoverBytes_.end(), message, message + messageSize);
        message += messageSize;
        messageSize = 0;
        return false;
    }
}

void
HasherSha256Builtin::accumulateChunk(const uint32_t chunk[16]) {
    using namespace IntegerOps;

    // Initialize the message schedule array, w
    uint32_t w[64];
    memcpy(w, chunk, 16*4);
    for (size_t i=16; i<64; ++i) {
        uint32_t sigma0 = rotateRight<32>(w[i-15],  7) ^ rotateRight<32>(w[i-15], 18) ^ (w[i-15] >>  3);
        uint32_t sigma1 = rotateRight<32>(w[i- 2], 17) ^ rotateRight<32>(w[i- 2], 19) ^ (w[i- 2] >> 10);
        w[i] = sigma1 + w[i-7] + sigma0 + w[i-16];
    }

    // Initialize local variables to be updated each round
    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    // Perform 64 rounds using the message schedule, w, initialized above
    for (size_t i=0; i<64; ++i) {
        const uint32_t s1 = rotateRight<32>(e, 6) ^ rotateRight<32>(e, 11) ^ rotateRight<32>(e, 25);
        const uint32_t choice = (e & f) ^ (~e & g);     // bit-wise "?:" operator
        const uint32_t temp1 = h + s1 + choice + roundConstants_[i] + w[i];
        const uint32_t s0 = rotateRight<32>(a, 2) ^ rotateRight<32>(a, 13) ^ rotateRight<32>(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c); // bit set if at least two inputs are set
        const uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    // Save the new state
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
    processedBytes_ += 16 * 4;
}

void
HasherSha256Builtin::append(const uint8_t *message, size_t messageSize) {
    ASSERT_require(message || 0 == messageSize);
    if (!digest_.empty())
        throw Exception("cannot append after returning digest");
    if (0 == messageSize)
        return;

    uint32_t chunk[16];
    while (getNextChunk(message /*in,out*/, messageSize /*in,out*/, chunk /*out*/))
        accumulateChunk(chunk);
}

const Hasher::Digest&
HasherSha256Builtin::digest() {
    if (digest_.empty()) {
        // Pad the message by appending an 0x80 followed by zero bytes, followed by the 8-byte big-endian message length so
        // that the total length (message length plus paddng) is a multiple of 64 bytes.
        const size_t messageSizeBytes = processedBytes_ + leftoverBytes_.size();
        const size_t nZeroPadding = 64 - (messageSizeBytes + 1 /*0x80*/ + 8 /*length*/) % 64;
        ASSERT_require((messageSizeBytes + 1 + nZeroPadding + 8) % 64 == 0);

        // Create and hash the padding
        uint8_t padding[64];
        padding[0] = 0x80;
        memset(padding+1, 0, nZeroPadding);
        const uint64_t messageSizeBits = 8 * messageSizeBytes;
        padding[1 + nZeroPadding + 0] = messageSizeBits >> 56;
        padding[1 + nZeroPadding + 1] = messageSizeBits >> 48;
        padding[1 + nZeroPadding + 2] = messageSizeBits >> 40;
        padding[1 + nZeroPadding + 3] = messageSizeBits >> 32;
        padding[1 + nZeroPadding + 4] = messageSizeBits >> 24;
        padding[1 + nZeroPadding + 5] = messageSizeBits >> 16;
        padding[1 + nZeroPadding + 6] = messageSizeBits >> 8;
        padding[1 + nZeroPadding + 7] = messageSizeBits;
        append(padding, 1 + nZeroPadding + 8);
        ASSERT_require(leftoverBytes_.size() == 0);

        // The digest is the concatenation of the state words
        for (size_t i=0; i<8; ++i) {
            digest_.push_back(state_[i] >> 24);
            digest_.push_back(state_[i] >> 16);
            digest_.push_back(state_[i] >> 8);
            digest_.push_back(state_[i]);
        }
    }
    return digest_;
}

} // namespace
} // namespace
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ROSE_DLL_API std::ostream&
operator<<(std::ostream &out, Rose::Combinatorics::Hasher &hasher) {
    hasher.print(out);
    return out;
}

