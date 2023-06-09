#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/netfilter.h>		/* for NF_ACCEPT */
#include <errno.h>

#include <libnetfilter_queue/libnetfilter_queue.h>

#define MAX_LENGTH 80
#define MAX_BUFFSIZE 100
#define HASH_ROUND 10
#define HASH_FIRSTKEY 0xFC9
#define TABLE_SIZE 100000000

char exist[TABLE_SIZE] = {0, };
unsigned long long int hash(char* line, int len)
{
	unsigned long long int num = 0;
	unsigned long long int key = HASH_FIRSTKEY;
	int i;
	int round = HASH_ROUND;
	int jump = len / HASH_ROUND;
	if (jump == 0)
	{
		jump = 1;
		round = len;
	}
	for (i = 0; i < round; i++)
	{
		num = (key << 12) | key;
		num = num + line[i * jump] * 0x9003;
		num = num % TABLE_SIZE;
		key = num;
	}
	return num;
}

int warning;
unsigned char * jmp_to_http(unsigned char *p, int maxlen)
{
	unsigned char *now = p;
	unsigned char ip_hdrlen, tcp_hdrlen;
	ip_hdrlen = *now & 0x0F;
	ip_hdrlen *= 4;
	now += ip_hdrlen;
	tcp_hdrlen = *(now+12) >> 4;
	tcp_hdrlen *= 4;
	now += tcp_hdrlen;
	if(now - p >= maxlen) return NULL;
	return now;
}

void dump(unsigned char* buf, int size) {
	int i;
	//printf("\n");
	for (i = 0; i < size; i++) {
		if (i != 0 && i % 16 == 0)
			printf("\n");
		printf("%02X ", buf[i]);
	}
	//printf("\n");
}

/* returns packet id */
static u_int32_t print_pkt (struct nfq_data *tb)
{
	int id = 0;
	struct nfqnl_msg_packet_hdr *ph;
	struct nfqnl_msg_packet_hw *hwph;
	u_int32_t mark,ifi;
	int ret;
	unsigned char *data;

	ph = nfq_get_msg_packet_hdr(tb);
	if (ph) {
		id = ntohl(ph->packet_id);
		/*
		printf("hw_protocol=0x%04x hook=%u id=%u ",
			ntohs(ph->hw_protocol), ph->hook, id);
			*/
	}

	hwph = nfq_get_packet_hw(tb);
	if (hwph) {
		int i, hlen = ntohs(hwph->hw_addrlen);
		/*
		printf("hw_src_addr=");
		for (i = 0; i < hlen-1; i++)
			printf("%02x:", hwph->hw_addr[i]);
		printf("%02x ", hwph->hw_addr[hlen-1]);
		*/
	}

	mark = nfq_get_nfmark(tb);
	/*
	if (mark)
		printf("mark=%u ", mark);*/

	ifi = nfq_get_indev(tb);
	/*
	if (ifi)
		printf("indev=%u ", ifi);*/

	ifi = nfq_get_outdev(tb);
	/*
	if (ifi)
		printf("outdev=%u ", ifi);*/

	ifi = nfq_get_physindev(tb);
	/*
	if (ifi)
		printf("physindev=%u ", ifi);*/

	ifi = nfq_get_physoutdev(tb);
	/*
	if (ifi)
		printf("physoutdev=%u ", ifi);*/

	ret = nfq_get_payload(tb, &data);
	if (ret >= 0)
	{	
		unsigned char * point = jmp_to_http(data, ret);
		if(point != NULL)
		{
			
			while(point - data + 8 < ret)
			{
				point = point + 1;
				if(*(point-1) == 0x0d && *point == 0x0a && *(point+1) == 'H' && *(point+2) == 'o' && *(point+3) == 's' && *(point+4) == 't')
				{
					char buf[MAX_BUFFSIZE] = {0, };
					int i = 0;
					point += 7;
					while(point-data < ret)
					{
						if(*point == 0x0d && *(point+1) == 0x0a) break;
						buf[i] = *point;
						i++;
						point += 1;
					}
					printf("%s\n", buf);
					unsigned long long int num = hash(buf, strlen(buf));
					if(exist[num] != 0)
					{
						printf("%s - ", buf);
						warning = 1;
					}
					break;
				}
			}
		}
		//dump(data, ret);
		//printf("npayload_len=%d\n", ret);
	}
	//fputc('\n', stdout);

	return id;
}


static int cb(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg,
	      struct nfq_data *nfa, void *data)
{
	warning = 0;
	u_int32_t id = print_pkt(nfa);
	//printf("entering callback\n");
	if(warning == 1)
	{
		printf("[Warning] Block suspicious sites!\n");
		return nfq_set_verdict(qh, id, NF_DROP, 0, NULL);
	}
	return nfq_set_verdict(qh, id, NF_ACCEPT, 0, NULL);
}

int main(int argc, char **argv)
{
	struct nfq_handle *h;
	struct nfq_q_handle *qh;
	struct nfnl_handle *nh;
	int fd;
	int rv;
	char buf[4096] __attribute__ ((aligned));

	if(argc != 2)
	{
		printf("syntax error\n");
		printf("syntax : netfilter-test <filename>\nsample : netfilter-test ban.txt");
		return -1;
	}


	// #########pre-work############
	
	unsigned int num = 0;
	int len = 0;
	char* FileName = argv[1];
	FILE* file = fopen(FileName, "r");
		if (file == NULL)
	{
		printf("FILE Read Error\n");
		return -1;
	}
	char line[MAX_LENGTH];
	unsigned int cnt = 0;
	while (fgets(line, MAX_LENGTH, file) != NULL)
	{
		cnt++;
		len = strlen(line);
		if (line[len - 1] == '\n') len-= 2;
		num = hash(line, len);
		exist[num] += 1;
	}
	printf("Complete Pre-work![%d]\n", cnt);


	printf("opening library handle\n");
	h = nfq_open();
	if (!h) {
		fprintf(stderr, "error during nfq_open()\n");
		exit(1);
	}

	printf("unbinding existing nf_queue handler for AF_INET (if any)\n");
	if (nfq_unbind_pf(h, AF_INET) < 0) {
		fprintf(stderr, "error during nfq_unbind_pf()\n");
		exit(1);
	}

	printf("binding nfnetlink_queue as nf_queue handler for AF_INET\n");
	if (nfq_bind_pf(h, AF_INET) < 0) {
		fprintf(stderr, "error during nfq_bind_pf()\n");
		exit(1);
	}

	printf("binding this socket to queue '0'\n");
	qh = nfq_create_queue(h,  0, &cb, NULL);
	if (!qh) {
		fprintf(stderr, "error during nfq_create_queue()\n");
		exit(1);
	}

	printf("setting copy_packet mode\n");
	if (nfq_set_mode(qh, NFQNL_COPY_PACKET, 0xffff) < 0) {
		fprintf(stderr, "can't set packet_copy mode\n");
		exit(1);
	}

	fd = nfq_fd(h);

	for (;;) {
		if ((rv = recv(fd, buf, sizeof(buf), 0)) >= 0) {
			//printf("pkt received\n");
			nfq_handle_packet(h, buf, rv);
			continue;
		}
		/* if your application is too slow to digest the packets that
		 * are sent from kernel-space, the socket buffer that we use
		 * to enqueue packets may fill up returning ENOBUFS. Depending
		 * on your application, this error may be ignored. nfq_nlmsg_verdict_putPlease, see
		 * the doxygen documentation of this library on how to improve
		 * this situation.
		 */
		if (rv < 0 && errno == ENOBUFS) {
			printf("losing packets!\n");
			continue;
		}
		perror("recv failed");
		break;
	}

	printf("unbinding from queue 0\n");
	nfq_destroy_queue(qh);

#ifdef INSANE
	/* normally, applications SHOULD NOT issue this command, since
	 * it detaches other programs/sockets from AF_INET, too ! */
	printf("unbinding from AF_INET\n");
	nfq_unbind_pf(h, AF_INET);
#endif

	printf("closing library handle\n");
	nfq_close(h);

	exit(0);
}

