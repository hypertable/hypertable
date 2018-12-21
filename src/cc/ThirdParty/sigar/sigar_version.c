#include "sigar.h"

static sigar_version_t sigar_version = {
    "",
    "",
    "libHyperThirdParty-sigar-1.6.4",
    "",
    "",
    "",
    "libHyperThirdParty-sigar-1.6.4"
    ""
    "",
    1,
    6,
    4,
    0
};

SIGAR_DECLARE(sigar_version_t *) sigar_version_get(void)
{
    return &sigar_version;
}
