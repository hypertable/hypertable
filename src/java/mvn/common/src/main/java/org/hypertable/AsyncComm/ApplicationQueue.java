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

package org.hypertable.AsyncComm;

import java.util.logging.Logger;
import java.util.LinkedList;
import java.util.ListIterator;
import java.util.HashMap;

public class ApplicationQueue {

    static final Logger log = Logger.getLogger("org.hypertable");

    static private boolean ms_shutdown = false;

    private static class UsageRec {
        long    threadGroup = 0;
        boolean running = false;
        int     outstanding = 1;
    }

    private static class WorkRec {
        ApplicationHandler appHandler = null;
        UsageRec usage = null;
    }

    private static class Worker implements Runnable {

        LinkedList<WorkRec> mQueue;
        HashMap<Long, UsageRec> mUsageMap;

        Worker(LinkedList<WorkRec> workQueue, HashMap<Long, UsageRec>
               usageMap) {
            mQueue    = workQueue;
            mUsageMap = usageMap;
        }

        public void run() {
            WorkRec rec = null;
            ListIterator<WorkRec> iter;

            try {

                while (!Thread.interrupted()) {

                    synchronized (mQueue) {

                        while (mQueue.isEmpty()) {
                            if (ms_shutdown)
                                return;
                            mQueue.wait();
                        }

                        iter = mQueue.listIterator(0);
                        synchronized (mUsageMap) {
                            while (iter.hasNext()) {
                                rec = iter.next();
                                if (rec.usage == null || !rec.usage.running) {
                                    if (rec.usage != null)
                                        rec.usage.running = true;
                                    iter.remove();
                                    break;
                                }
                                rec = null;
                            }
                        }
                        if (rec == null) {
                            if (ms_shutdown)
                                return;
                            mQueue.wait();
                            if (ms_shutdown)
                                return;
                        }
                    }

                    if (rec != null) {
                        try {
                            rec.appHandler.run();
                        }
                        catch (Exception e) {
                            e.printStackTrace();
                        }
                        finally {
                            if (rec.usage != null) {
                                synchronized (mUsageMap) {
                                    rec.usage.running = false;
                                    rec.usage.outstanding--;
                                    if (rec.usage.outstanding == 0) {
                                        mUsageMap.remove(rec.usage.threadGroup);
                                    }
                                }
                            }
                        }
                    }
                }

            }
            catch (InterruptedException e) {
            }
            catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    private LinkedList<WorkRec>     mQueue = new LinkedList<WorkRec>();
    private HashMap<Long, UsageRec> mUsageMap = new HashMap<Long, UsageRec>();
    private Thread [] threads = null;

    public ApplicationQueue(int workerCount) {
        assert (workerCount > 0);
        threads = new Thread[workerCount];
        for (int i=0; i<workerCount; i++) {
            Worker worker = new Worker(mQueue, mUsageMap);
            threads[i] = new Thread(worker, "ApplicationQueueThread " + i);
            threads[i].start();
        }
    }

    public void Join() {
        try {
            for (int i=0; i<threads.length; i++) {
                threads[i].join();
            }
        }
        catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    public void Shutdown() {
        ms_shutdown = true;
        for (int i=0; i<threads.length; i++) {
            threads[i].interrupt();
        }
    }

    public void Add(ApplicationHandler appHandler) {
        long threadGroup = appHandler.GetThreadGroup();
        WorkRec rec = new WorkRec();
        rec.appHandler = appHandler;
        synchronized (mUsageMap) {
            rec.usage = mUsageMap.get(threadGroup);
            if (rec.usage != null)
                rec.usage.outstanding++;
            else {
                rec.usage = new UsageRec();
                rec.usage.threadGroup = threadGroup;
                mUsageMap.put(threadGroup, rec.usage);
            }
        }
        synchronized (mQueue) {
            mQueue.addLast(rec);
            mQueue.notify();
        }
    }
}

