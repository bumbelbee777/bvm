// SSX crypto: AES-128 block encrypt/decrypt (FIPS-197 vector) + deterministic XGEN.
// Plaintext/key/ciphertext from NIST AES-128 example; XGEN seed 0xC0FFEE checked in RAM.

.data
    Plaintext: .quad 0x0011223344556677, 0x889900AABBCCDDEE
    Key: .quad 0x0001020304050607, 0x08090A0B0C0D0E0F
    ExpectedCipher: .quad 0x69C4E0D24710AB44, 0x3AC346766BCA8C1C
    CipherBuf: .quad 0, 0
    PlainBuf: .quad 0, 0
    XgenBuf: .quad 0, 0
    XgenExpected: .quad 0xECE45BABCE870479, 0xCA8216FA9058D0FA

.text
BootEntry:
    xload x0, [Plaintext]
    xload x1, [Key]
    xcrypt x2, x0, x1
    xstore x2, [CipherBuf]
    xload x3, [ExpectedCipher]
    xcmp x2, x3
    jne BadOutcome

    xdecrypt x4, x2, x1
    xcmp x4, x0
    jne BadOutcome
    xstore x4, [PlainBuf]

    xgen x5, #0xC0FFEE
    xstore x5, [XgenBuf]
    xload x6, [XgenExpected]
    xcmp x5, x6
    jne BadOutcome

    hlt

BadOutcome:
    load r10, #0
    hlt
