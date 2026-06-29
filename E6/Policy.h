#ifndef POLICY_H
#define POLICY_H

typedef struct ReplacementPolicy
{
    void *policy;
    char *(*insert)(void *policy, const char *key);
    void (*access)(void *policy, const char *key);
    void (*remove)(void *policy, const char *key);
    void (*destroy)(void *policy);
} ReplacementPolicy;

#endif