/* SPDX-License-Identifier: MIT */
#include "combined-core.h"
int lgcb_mx_plan_eligible(const struct lgmp_result *r)
{
    return r && !r->error && r->decoded_valid && r->stable && r->completed_sweeps==2u &&
        r->voltage_fields_valid && r->bound_850 && r->required_uv>=350000u && r->required_uv<=850000u;
}
int lgcb_bus_facts(const struct inherited_samples *s,struct lgcb_bus_facts *f)
{
    if(!s || !f)return -1;
    *f=(struct lgcb_bus_facts){0};
    uint32_t mask=((UINT32_C(1)<<INHERITED_WORDS)-1u)&~(UINT32_C(1)<<IH_BKE);
    if((s->valid[0]&mask)!=mask || (s->valid[1]&mask)!=mask)return -1;
    for(unsigned i=0;i<INHERITED_WORDS;++i)if(i!=IH_BKE && s->word[0][i]!=s->word[1][i])return -1;
    f->stable=1; f->branches_on=1;
    const unsigned branch[3]={IH_BIMC_GFX,IH_MMSS_BIMC_GFX,IH_MMAGIC_AHB};
    for(unsigned i=0;i<3u;++i) {
        uint32_t v=s->word[1][branch[i]],state=v>>28;
        if(!(v&1u) || (state!=0u && state!=2u))f->branches_on=0;
    }
    /* Decode inherited configuration only, independently of the cold local
     * gate/root status. No modified sample is exposed as hardware evidence.
     * The real parent-gate helper subsequently requires the branch ON. */
    uint32_t config[INHERITED_WORDS];
    for(unsigned i=0;i<INHERITED_WORDS;++i)config[i]=s->word[1][i];
    config[IH_MMAGIC_AHB]=1u;
    config[IH_AHB_CMD]&=~UINT32_C(0x80000000);
    f->ahb_valid=!inherited_ahb_hz(config,&f->ahb_hz,&f->ahb_parent);
    /* Cold CBCR halt is an observation, not a prerequisite for acquiring a
     * local parent vote. In particular 0x309010=0x80004221 is valid here.
     * The parent helper must establish MMAGIC AHB ON before GPU AHB use.
     * Unknown parent configuration remains ahb_valid=0 for the caller. */
    return 0;
}
int lgcb_platform_source_blocker(void) { return LGCB_S1_UNBOUND; }
