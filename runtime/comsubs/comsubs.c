/* Clean-room COMSUBS.LIB replacement (WP-5) */

unsigned int cm_invalid_parm_pointer = 0;
char cm_flhandle_enable_sw = 0;
char cm_trace_cmd_flg = 0;
unsigned int current_char = 0;
unsigned int previous_char = 0;
unsigned int checking_state = 0;
unsigned int invalid_parm_flag = 0;
unsigned int drive_error_flag = 0;
unsigned int syntax_error_flag = 0;
unsigned int msg_buf_size = 512;
unsigned int init_req = 0;

static unsigned char dbcs_lead_tbl[8] = { 0 };

void com_settbl(void) {
}

int check_dbcs(unsigned char c) {
    unsigned int i = 0;
    while (i + 1 < sizeof(dbcs_lead_tbl) &&
           (dbcs_lead_tbl[i] != 0 || dbcs_lead_tbl[i + 1] != 0)) {
        if (c >= dbcs_lead_tbl[i] && c <= dbcs_lead_tbl[i + 1]) {
            return 1;
        }
        i += 2;
    }
    return 0;
}

int com_toupper(unsigned char c) {
    if (c >= 'a' && c <= 'z') {
        return (int)(c - ('a' - 'A'));
    }
    return (int)c;
}

int com_tolower(unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
        return (int)(c + ('a' - 'A'));
    }
    return (int)c;
}

unsigned char *com_strupr(unsigned char *s) {
    unsigned char *p = s;
    while (*p != '\0') {
        if (check_dbcs(*p) && p[1] != '\0') {
            p += 2;
            continue;
        }
        *p = (unsigned char)com_toupper(*p);
        ++p;
    }
    return s;
}

unsigned char *com_strlwr(unsigned char *s) {
    unsigned char *p = s;
    while (*p != '\0') {
        if (check_dbcs(*p) && p[1] != '\0') {
            p += 2;
            continue;
        }
        *p = (unsigned char)com_tolower(*p);
        ++p;
    }
    return s;
}

char *com_strchr(unsigned char *s, unsigned char c) {
    while (*s != '\0') {
        if (*s == c) {
            return (char *)s;
        }
        if (check_dbcs(*s) && s[1] != '\0') {
            s += 2;
            continue;
        }
        ++s;
    }
    return (c == '\0') ? (char *)s : (char *)0;
}

unsigned char *com_strrchr(unsigned char *s, unsigned char c) {
    unsigned char *last = (unsigned char *)0;
    while (*s != '\0') {
        if (*s == c) {
            last = s;
        }
        if (check_dbcs(*s) && s[1] != '\0') {
            s += 2;
            continue;
        }
        ++s;
    }
    if (c == '\0') {
        return s;
    }
    return last;
}

int com_strcmpi(unsigned char *a, unsigned char *b) {
    int ca;
    int cb;
    for (;;) {
        ca = com_toupper(*a);
        cb = com_toupper(*b);
        if (ca != cb || ca == 0) {
            return ca - cb;
        }
        ++a;
        ++b;
    }
}

unsigned char *com_substr(unsigned char *s, unsigned char *sub) {
    unsigned int i;
    if (*sub == '\0') {
        return s;
    }
    while (*s != '\0') {
        i = 0;
        while (sub[i] != '\0' && s[i] == sub[i]) {
            ++i;
        }
        if (sub[i] == '\0') {
            return s;
        }
        if (check_dbcs(*s) && s[1] != '\0') {
            s += 2;
        } else {
            ++s;
        }
    }
    return (unsigned char *)0;
}

unsigned int comcheckdosver(void) {
    return 0;
}

unsigned int rctomid(unsigned int rc) {
    return rc;
}

unsigned int checkdrive(unsigned char drv) {
    (void)drv;
    return 0;
}

unsigned int comgetarg(
    unsigned int *argc_p,
    char **argv,
    char **new_argv,
    unsigned int max_args,
    char *arg_buf,
    unsigned int buf_size,
    char *def_drive,
    char *sw_char) {
    unsigned int i;
    unsigned int out_idx = 0;
    unsigned int buf_pos = 0;
    (void)def_drive;
    (void)sw_char;
    if (argc_p == (unsigned int *)0) {
        return 1;
    }
    for (i = 0; i < *argc_p && out_idx < max_args; ++i) {
        char *src = argv[i];
        new_argv[out_idx++] = &arg_buf[buf_pos];
        while (*src != '\0' && buf_pos + 1 < buf_size) {
            arg_buf[buf_pos++] = *src++;
        }
        arg_buf[buf_pos++] = '\0';
    }
    *argc_p = out_idx;
    return 0;
}

unsigned int computmsg(
    char **inserts,
    unsigned int num_inserts,
    unsigned int msg_id,
    char *msg_file,
    unsigned int dev_type,
    unsigned int resp_type,
    char *resp_buf,
    unsigned int resp_size) {
    (void)inserts;
    (void)num_inserts;
    (void)msg_id;
    (void)msg_file;
    (void)dev_type;
    (void)resp_type;
    if (resp_buf != (char *)0 && resp_size > 0) {
        resp_buf[0] = '\0';
    }
    return 0;
}

unsigned int getmessage(unsigned int msg_id, char *buf, unsigned int size) {
    (void)msg_id;
    if (buf != (char *)0 && size > 0) {
        buf[0] = '\0';
    }
    return 0;
}

void setnewargv(void) {
}
