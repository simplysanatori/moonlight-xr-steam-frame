/* XML access for Sunshine/GFE responses on top of libxml2 (a SteamOS package).
 * Mirrors NvHTTP::getXmlString: the first element with a given name, in
 * document order, at any depth. Entity expansion and network access are off. GPLv3. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

struct mlxr_xml {
    xmlDocPtr doc;
};

static void quiet(void *ctx, const char *msg, ...) { (void)ctx; (void)msg; }

mlxr_xml *mlxr_xml_parse(const char *xml, size_t len)
{
    if (!xml || len == 0 || len > (8u << 20)) return NULL;
    xmlInitParser();
    xmlSetGenericErrorFunc(NULL, quiet);
    /* RECOVER: GFE has been seen to emit slightly malformed documents. */
    xmlDocPtr doc = xmlReadMemory(xml, (int)len, "response.xml", NULL,
                                  XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR | XML_PARSE_RECOVER);
    if (!doc || !xmlDocGetRootElement(doc)) {
        if (doc) xmlFreeDoc(doc);
        return NULL;
    }
    mlxr_xml *x = calloc(1, sizeof *x);
    if (!x) {
        xmlFreeDoc(doc);
        return NULL;
    }
    x->doc = doc;
    return x;
}

void mlxr_xml_free(mlxr_xml *x)
{
    if (!x) return;
    xmlFreeDoc(x->doc);
    free(x);
}

static xmlNodePtr next_in_order(xmlNodePtr n)
{
    if (n->children) return n->children;
    while (n) {
        if (n->next) return n->next;
        n = n->parent;
    }
    return NULL;
}

static bool is_el(xmlNodePtr n, const char *tag)
{
    return n->type == XML_ELEMENT_NODE && n->name && strcmp((const char *)n->name, tag) == 0;
}

static xmlNodePtr find_nth(const mlxr_xml *x, const char *tag, size_t idx)
{
    size_t i = 0;
    for (xmlNodePtr n = xmlDocGetRootElement(x->doc); n; n = next_in_order(n)) {
        if (is_el(n, tag) && i++ == idx) return n;
    }
    return NULL;
}

static char *text_of(xmlNodePtr n)
{
    xmlChar *c = xmlNodeGetContent(n);
    char *r = strdup(c ? (const char *)c : "");
    if (c) xmlFree(c);
    return r;
}

char *mlxr_xml_get(const mlxr_xml *x, const char *tag)
{
    if (!x) return NULL;
    xmlNodePtr n = find_nth(x, tag, 0);
    return n ? text_of(n) : NULL;
}

size_t mlxr_xml_count(const mlxr_xml *x, const char *tag)
{
    size_t c = 0;
    if (!x) return 0;
    for (xmlNodePtr n = xmlDocGetRootElement(x->doc); n; n = next_in_order(n))
        if (is_el(n, tag)) c++;
    return c;
}

char *mlxr_xml_get_nth(const mlxr_xml *x, const char *tag, size_t idx, const char *child)
{
    if (!x) return NULL;
    xmlNodePtr n = find_nth(x, tag, idx);
    if (!n) return NULL;
    for (xmlNodePtr c = n->children; c; c = c->next)
        if (is_el(c, child)) return text_of(c);
    return NULL;
}

bool mlxr_xml_status(const mlxr_xml *x, int *code, char *msg, size_t cap)
{
    if (!x) return false;
    xmlNodePtr root = xmlDocGetRootElement(x->doc);
    if (!root || !is_el(root, "root")) {
        /* moonlight-qt scans for any <root>; be equally tolerant */
        root = find_nth(x, "root", 0);
        if (!root) return false;
    }
    xmlChar *sc = xmlGetProp(root, (const xmlChar *)"status_code");
    xmlChar *sm = xmlGetProp(root, (const xmlChar *)"status_message");
    /* status_code can be 0xFFFFFFFF on GFE 3.20.3: parse unsigned, cast to int */
    if (code) *code = sc ? (int)(unsigned int)strtoull((const char *)sc, NULL, 10) : 0;
    if (msg && cap) mlxr_strlcpy(msg, sm ? (const char *)sm : "", cap);
    if (sc) xmlFree(sc);
    if (sm) xmlFree(sm);
    return true;
}
