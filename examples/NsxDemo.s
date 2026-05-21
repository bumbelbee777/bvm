// NSX smoke: RELU, GELU, SOFTMAX, nconv2d (0x621). Success: R10 = 0x41200000.

.data
    Input: .quad 0xBF80000000000000
    .quad 0x3F80000040000000
    Kernel: .quad 0x3F8000003F800000
    .quad 0x3F8000003F800000
    ReluOut: .quad 0
    OutConv: .quad 0
    InputTensorDesc: .quad Input
    .quad 0
    .quad 0
    .quad 0x4000200020000
    ReluStoreDesc: .quad ReluOut
    .quad 0
    .quad 0
    .quad 0x4000200020000
    NsxConvDesc: .quad Input
    .quad Kernel
    .quad OutConv
    .quad 0x0002000200010202
    .quad 0
    .quad 0
    .quad 0

.text
BootEntry:
    tensor_load x0, InputTensorDesc
    relu x1, x0
    tensor_store x1, ReluStoreDesc
    load r10, [ReluOut]
    cmp r10, #0
    jne BadOutcome
    gelu x2, x0
    softmax x3, x0
    nconv2d x4, NsxConvDesc
    load r10, [OutConv]
    cmp r10, #0x40000000
    jne BadOutcome
    load r10, #0x41200000
    hlt

BadOutcome:
    load r10, #0
    hlt
