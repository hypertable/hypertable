/**
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
package org.hypertable.Common;

import java.io.File;
import java.io.IOException;

public class Checksum {

    public static int fletcher32(byte [] data, int offset, int len) {

        /* data may not be aligned properly and would segfault on
         * many systems if cast and used as 16-bit words
         */
        int datai = offset;
        int sum1 = 0xffff, sum2 = 0xffff;
        int len16 = len / 2; /* loop works on 16-bit words */

        while (len16 != 0) {
            /* 360 is the largest number of sums that can be
             * performed without integer overflow
             */
            int tlen = len16 > 360 ? 360 : len16;
            len16 -= tlen;

            if (tlen != 0) do {
                sum1 += (short)(data[datai] & 0x00FF) << 8
                        | (short)(data[datai+1] & 0x00FF);
                sum2 += sum1;
                datai += 2;
                tlen--;
            } while (tlen != 0);

            sum1 = (sum1 & 0xffff) + (sum1 >> 16);
            sum2 = (sum2 & 0xffff) + (sum2 >> 16);
        }

        /* Check for odd number of bytes */
        if ((len & 1) != 0) {
            short rem = (short)(data[datai] & 0x00FF);
            rem <<= 8;
            sum1 += rem;
            sum2 += sum1;
            sum1 = (sum1 & 0xffff) + (sum1 >> 16);
            sum2 = (sum2 & 0xffff) + (sum2 >> 16);
        }

        /* Second reduction step to reduce sums to 16 bits */
        sum1 = (sum1 & 0xffff) + (sum1 >> 16);
        sum2 = (sum2 & 0xffff) + (sum2 >> 16);
        return (sum2 << 16) | sum1;
    }

    static String usage[] = {
        "",
        "usage: java org.hypertable.Common.Checksum <algorithm> <file>",
        "",
        "This program runs the checksum algorithm, <algorithm>, over the bytes",
        "of the input file <file> and displays the resulting checksum as a",
        "signed integer.",
        "",
        "Supported Algorithms:",
        "",
        "  fletcher32",
        "",
        null
    };


    public static void main(String [] args) throws IOException {

        if (args.length != 2)
            Usage.DumpAndExit(usage);

        if (args[0].equals("fletcher32")) {
            byte [] data = FileUtils.FileToBuffer( new File(args[1]) );
            int checksum = fletcher32(data, 0, data.length);
            java.lang.System.out.println(checksum);
        }
        else
            Usage.DumpAndExit(usage);
    }

}

