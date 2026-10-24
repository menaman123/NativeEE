// vu_block_tier.inl -- the block tier's inline per-op helper, shared by the
// loop (RunOps) and the AOT generator's emitted blocks. Included from
// vu_block_tier.h only.
inline void VuBlockTier::PinHardwired(VUregs& regs) {
    regs.VF[0].x = 0.0f;
    regs.VF[0].y = 0.0f;
    regs.VF[0].z = 0.0f;
    regs.VF[0].w = 1.0f;
    regs.VI[0] = 0;
}
