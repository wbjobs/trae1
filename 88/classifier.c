/* SPDX-License-Identifier: BSD-3-Clause
 * Traffic Classifier - port-based + DPI (HTTP Host regex)
 *
 * Classification order:
 *   1. Port-based rules (fast path)
 *   2. DPI regex match on TCP payload (HTTP Host header)
 *   3. Fallback to UNKNOWN
 *
 * Categories: Web, Video, P2P, Game, VoIP
 */
#include "te_header.h"
#include <pcre.h>

/* ------------------------------------------------------------------ */
/*  Port rules                                                        */
/* ------------------------------------------------------------------ */
struct port_rule {
    uint16_t port;
    uint8_t  protocol;   /* IPPROTO_TCP, IPPROTO_UDP, or 0 for both */
    uint8_t  category;   /* enum te_category */
};

static const struct port_rule g_port_rules[] = {
    /* Web */
    { 80,   IPPROTO_TCP, TE_CAT_WEB  },
    { 443,  IPPROTO_TCP, TE_CAT_WEB  },
    { 8080, IPPROTO_TCP, TE_CAT_WEB  },
    { 8000, IPPROTO_TCP, TE_CAT_WEB  },

    /* Video (RTSP/RTMP/HLS often via 443/1935/554) */
    { 1935, IPPROTO_TCP, TE_CAT_VIDEO},
    { 554,  IPPROTO_TCP, TE_CAT_VIDEO},
    { 8001, IPPROTO_TCP, TE_CAT_VIDEO},

    /* P2P (BitTorrent/eMule) */
    { 6881, IPPROTO_TCP, TE_CAT_P2P  },
    { 6882, IPPROTO_TCP, TE_CAT_P2P  },
    { 6883, IPPROTO_TCP, TE_CAT_P2P  },
    { 6884, IPPROTO_TCP, TE_CAT_P2P  },
    { 6885, IPPROTO_TCP, TE_CAT_P2P  },
    { 6886, IPPROTO_TCP, TE_CAT_P2P  },
    { 6887, IPPROTO_TCP, TE_CAT_P2P  },
    { 6888, IPPROTO_TCP, TE_CAT_P2P  },
    { 6889, IPPROTO_TCP, TE_CAT_P2P  },
    { 4662, IPPROTO_TCP, TE_CAT_P2P  },
    { 4672, IPPROTO_UDP, TE_CAT_P2P  },

    /* Game */
    { 27015, IPPROTO_UDP, TE_CAT_GAME },  /* Steam/CS */
    { 27016, IPPROTO_UDP, TE_CAT_GAME },
    { 3724,  IPPROTO_TCP, TE_CAT_GAME },  /* WoW */
    { 6112,  IPPROTO_TCP, TE_CAT_GAME },  /* Battle.net */
    { 6112,  IPPROTO_UDP, TE_CAT_GAME },

    /* VoIP */
    { 5060, IPPROTO_UDP, TE_CAT_VOIP },   /* SIP */
    { 5060, IPPROTO_TCP, TE_CAT_VOIP },
    { 5061, IPPROTO_TCP, TE_CAT_VOIP },   /* SIP/TLS */
    { 3478, IPPROTO_UDP, TE_CAT_VOIP },   /* STUN */
    { 3479, IPPROTO_UDP, TE_CAT_VOIP },
    { 10000, IPPROTO_UDP, TE_CAT_VOIP },  /* RTP */
    { 16384, IPPROTO_UDP, TE_CAT_VOIP },
    { 32768, IPPROTO_UDP, TE_CAT_VOIP },
};

#define G_PORT_RULES_N RTE_DIM(g_port_rules)

/* ------------------------------------------------------------------ */
/*  DPI: HTTP Host header regex                                       */
/* ------------------------------------------------------------------ */
struct dpi_rule {
    const char *name;
    const char *pattern;
    uint8_t     category;
    pcre       *re;
    pcre_extra *re_extra;
};

/* Patterns match the Host header value (case insensitive) */
static struct dpi_rule g_dpi_rules[] = {
    {
        .name     = "Web-Generic",
        .pattern  = "(?i)^Host:\\s+(www\\.|[a-z0-9.-]+\\.(com|net|org|io|cn|ru|jp|de|uk))",
        .category = TE_CAT_WEB,
        .re       = NULL,
        .re_extra = NULL,
    },
    {
        .name     = "Video-Streaming",
        .pattern  = "(?i)Host:\\s+.*(youtube|youtu\\.be|vimeo|dailymotion|bilibili|iqiyi|youku|tudou|hulu|netflix|twitch)",
        .category = TE_CAT_VIDEO,
        .re       = NULL,
        .re_extra = NULL,
    },
    {
        .name     = "P2P-Tracker",
        .pattern  = "(?i)Host:\\s+.*(tracker|torrent|thepiratebay|rutracker)",
        .category = TE_CAT_P2P,
        .re       = NULL,
        .re_extra = NULL,
    },
    {
        .name     = "Game-Server",
        .pattern  = "(?i)Host:\\s+.*(battle\\.net|steam|ea\\.com|riotgames|epicgames)",
        .category = TE_CAT_GAME,
        .re       = NULL,
        .re_extra = NULL,
    },
    {
        .name     = "VoIP-Signaling",
        .pattern  = "(?i)^(INVITE|REGISTER|BYE|ACK)\\s+sip:",
        .category = TE_CAT_VOIP,
        .re       = NULL,
        .re_extra = NULL,
    },
};

#define G_DPI_RULES_N RTE_DIM(g_dpi_rules)

int te_classifier_init(void)
{
    for (uint32_t i = 0; i < G_DPI_RULES_N; i++) {
        const char *err;
        int erroff;
        g_dpi_rules[i].re = pcre_compile(
            g_dpi_rules[i].pattern,
            0, &err, &erroff, NULL);
        if (g_dpi_rules[i].re == NULL) {
            RTE_LOG(ERR, USER1,
                    "PCRE compile failed for '%s': %s at %d\n",
                    g_dpi_rules[i].name, err, erroff);
            return -1;
        }
        g_dpi_rules[i].re_extra = pcre_study(
            g_dpi_rules[i].re, 0, &err);
        if (g_dpi_rules[i].re_extra == NULL && err != NULL) {
            RTE_LOG(WARNING, USER1,
                    "PCRE study failed for '%s': %s\n",
                    g_dpi_rules[i].name, err);
        }
    }
    return 0;
}

void te_classifier_fini(void)
{
    for (uint32_t i = 0; i < G_DPI_RULES_N; i++) {
        if (g_dpi_rules[i].re_extra) pcre_free(g_dpi_rules[i].re_extra);
        if (g_dpi_rules[i].re)       pcre_free(g_dpi_rules[i].re);
        g_dpi_rules[i].re       = NULL;
        g_dpi_rules[i].re_extra = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Main classification routine                                       */
/* ------------------------------------------------------------------ */
enum te_category te_classify(const struct te_5tuple *key,
                              const uint8_t *l4_payload,
                              uint16_t payload_len)
{
    /* 1. Fast port-based match (both directions) */
    for (uint32_t i = 0; i < G_PORT_RULES_N; i++) {
        const struct port_rule *r = &g_port_rules[i];
        if (r->protocol != 0 && r->protocol != key->protocol)
            continue;
        if (r->port == key->src_port || r->port == key->dst_port)
            return (enum te_category)r->category;
    }

    /* 2. DPI regex on payload (only for TCP with non-empty payload) */
    if (key->protocol == IPPROTO_TCP &&
        l4_payload != NULL && payload_len > 0) {

        int ovec[30];

        for (uint32_t i = 0; i < G_DPI_RULES_N; i++) {
            int rc = pcre_exec(
                g_dpi_rules[i].re,
                g_dpi_rules[i].re_extra,
                (const char *)l4_payload,
                (int)payload_len,
                0, 0, ovec, RTE_DIM(ovec));
            if (rc >= 0)
                return (enum te_category)g_dpi_rules[i].category;
        }
    }

    return TE_CAT_UNKNOWN;
}
