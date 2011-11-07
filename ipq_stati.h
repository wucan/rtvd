#ifndef _IPQ_STATI_H_
#define _IPQ_STATI_H_


#define RATE_STATI_DEPTH        1024
struct ipq_rate_stati_data {
    int history[RATE_STATI_DEPTH];
    int next_index;
};

struct ipq_stati {
    struct ipq_rate_stati_data ebi_rate;
};

extern struct ipq_stati stati_data;


#endif /* _IPQ_STATI_H_ */

