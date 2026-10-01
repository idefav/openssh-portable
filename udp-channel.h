/* Private UDP forwarding extension. See PROTOCOL. */
Channel *udp_channel_new(struct ssh *, const char *, u_short,
    const char *, int, const char *, const char *);
void udp_channel_pre(struct ssh *, Channel *);
void udp_channel_post(struct ssh *, Channel *);
void udp_channel_free(Channel *);
time_t udp_channel_expiry(Channel *);
