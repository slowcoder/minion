#pragma once

const char *kvm_exitreason_str[] = {
"KVM_EXIT_UNKNOWN",
"KVM_EXIT_EXCEPTION",
"KVM_EXIT_IO",
"KVM_EXIT_HYPERCALL",
"KVM_EXIT_DEBUG",
"KVM_EXIT_HLT",
"KVM_EXIT_MMIO",
"KVM_EXIT_IRQ_WINDOW_OPEN",
"KVM_EXIT_SHUTDOWN",
"KVM_EXIT_FAIL_ENTRY",
"KVM_EXIT_INTR",
"KVM_EXIT_SET_TPR",
"KVM_EXIT_TPR_ACCESS",
"KVM_EXIT_S390_SIEIC",
"KVM_EXIT_S390_RESET",
"KVM_EXIT_DCR",
"KVM_EXIT_NMI",
"KVM_EXIT_INTERNAL_ERROR",
"KVM_EXIT_OSI",
"KVM_EXIT_PAPR_HCALL",
"KVM_EXIT_S390_UCONTROL",
"KVM_EXIT_WATCHDOG",
"KVM_EXIT_S390_TSCH",
"KVM_EXIT_EPR",
"KVM_EXIT_SYSTEM_EVENT",
"KVM_EXIT_S390_STSI",
"KVM_EXIT_IOAPIC_EOI",
};
