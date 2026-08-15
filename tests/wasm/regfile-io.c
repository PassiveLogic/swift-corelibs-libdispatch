/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

/*
 * dispatch_read / dispatch_write / DispatchIO on a regular file in a
 * preopened directory (the runner maps one with --preopen). Regular files
 * take the always-ready path - they are never polled - so this pins that
 * the io convenience APIs complete with the actual payload: write it out,
 * read it back whole, then a DISPATCH_IO_RANDOM byte-range read.
 */
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char payload[] = "regular-file-payload-0123456789";
#define PAYLOAD_LEN (sizeof(payload) - 1)

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.regio", NULL);
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);

	int wfd = open("/scratch/regfile-io.txt", O_CREAT | O_TRUNC | O_WRONLY,
			0644);
	if (wfd < 0) { printf("FAIL: open for write\n"); return 1; }
	dispatch_data_t data = dispatch_data_create(payload, PAYLOAD_LEN, queue,
			DISPATCH_DATA_DESTRUCTOR_DEFAULT);
	__block int write_error = -1;
	__block bool write_leftover = false;
	dispatch_write(wfd, data, queue, ^(dispatch_data_t unwritten, int error) {
		write_error = error;
		write_leftover = unwritten != NULL;
		dispatch_semaphore_signal(sem);
	});
	dispatch_release(data);
	dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	close(wfd);
	if (write_error || write_leftover) {
		printf("FAIL: dispatch_write error=%d leftover=%d\n", write_error,
				write_leftover);
		return 1;
	}
	printf("dispatch_write done\n");

	int rfd = open("/scratch/regfile-io.txt", O_RDONLY);
	if (rfd < 0) { printf("FAIL: open for read\n"); return 1; }
	static char got[64];
	__block int read_error = -1;
	__block size_t got_len = 0;
	dispatch_read(rfd, 4096, queue, ^(dispatch_data_t rdata, int error) {
		read_error = error;
		if (rdata) {
			dispatch_data_apply(rdata, ^bool(dispatch_data_t region,
					size_t offset, const void *buffer, size_t size) {
				(void)region;
				if (offset + size <= sizeof(got)) {
					memcpy(got + offset, buffer, size);
					got_len += size;
				}
				return true;
			});
		}
		dispatch_semaphore_signal(sem);
	});
	dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	close(rfd);
	if (read_error || got_len != PAYLOAD_LEN ||
			memcmp(got, payload, PAYLOAD_LEN) != 0) {
		printf("FAIL: dispatch_read error=%d len=%zu\n", read_error, got_len);
		return 1;
	}
	printf("dispatch_read payload OK\n");

	// random-access DispatchIO: bytes [13, 13+7) of the payload are "payload"
	int iofd = open("/scratch/regfile-io.txt", O_RDONLY);
	if (iofd < 0) { printf("FAIL: open for io\n"); return 1; }
	dispatch_io_t channel = dispatch_io_create(DISPATCH_IO_RANDOM, iofd,
			queue, ^(int error) { (void)error; });
	if (!channel) { printf("FAIL: dispatch_io_create\n"); return 1; }
	static char range[8];
	__block size_t range_len = 0;
	__block int io_error = -1;
	dispatch_io_read(channel, 13, 7, queue,
			^(bool done, dispatch_data_t rdata, int error) {
		if (rdata) {
			dispatch_data_apply(rdata, ^bool(dispatch_data_t region,
					size_t offset, const void *buffer, size_t size) {
				(void)region; (void)offset;
				if (range_len + size <= 7) {
					memcpy(range + range_len, buffer, size);
					range_len += size;
				}
				return true;
			});
		}
		if (done) {
			io_error = error;
			dispatch_semaphore_signal(sem);
		}
	});
	dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	dispatch_io_close(channel, 0);
	dispatch_release(channel);
	close(iofd);
	if (io_error || range_len != 7 || memcmp(range, "payload", 7) != 0) {
		printf("FAIL: io random error=%d len=%zu\n", io_error, range_len);
		return 1;
	}
	printf("io random range OK\n");
	printf("regular file io OK\n");
	return 0;
}
