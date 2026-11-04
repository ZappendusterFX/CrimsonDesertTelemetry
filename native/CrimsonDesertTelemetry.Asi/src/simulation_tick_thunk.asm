; Optional diagnostic ENTRY observation, not World Builder's completed-call
; counter. No C++ calls, arguments, GPRs, vector state or return address touched.
; Storage/module/trampoline remain resident until process termination.
EXTERN CdtSimulationTickCounter:QWORD
EXTERN CdtSimulationTickTrampoline:QWORD
.code
CdtSimulationTickThunk PROC FRAME
    pushfq
    .allocstack 8
    .endprolog
    lock inc QWORD PTR [CdtSimulationTickCounter]
    popfq
CdtSimulationTickThunk ENDP
; POPFQ is not a Windows unwind epilogue. End the RUNTIME_FUNCTION at its
; successor: the tail jump has the untouched entry stack and is a leaf region.
PUBLIC CdtSimulationTickTail
CdtSimulationTickTail LABEL BYTE
    jmp QWORD PTR [CdtSimulationTickTrampoline]
END
