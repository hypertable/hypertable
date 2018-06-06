/*
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

/** @file
 * Definition of MetricsCollectorGanglia.
 * This file contains the definition of MetricsCollectorGanglia, a class for
 * collection and publishing of Ganglia metrics.
 */

package org.hypertable.Common;

import java.lang.Double;
import java.lang.Integer;
import java.lang.String;
import java.lang.System;
import java.net.DatagramSocket;
import java.net.DatagramPacket;
import java.net.InetAddress;
import java.net.PortUnreachableException;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;
import java.net.UnknownHostException;
import java.net.SocketException;


/** Collector for Ganglia metrics.
 */
public class MetricsCollectorGanglia implements MetricsCollector {

  /** Constructor.
   * Creates a datagram send socket and connects it to the Ganglia hypertable
   * extension listen port.  Initializes mPrefix to
   * "ht." + <code>component</code> + ".".
   * @param component Hypertable component ("fsbroker", "hyperspace, "master",
   * "rangeserver", or "thriftbroker")
   * @param props Configuration properties
   */
  public MetricsCollectorGanglia(String component, Properties props) {
    mPrefix = "ht." + component + ".";

    String str = props.getProperty("Hypertable.Metrics.Ganglia.Port", "15860");
    mPort = Integer.parseInt(str);

    str = props.getProperty("Hypertable.Metrics.Ganglia.Disabled");
    if (str != null && str.equalsIgnoreCase("true"))
      mDisabled = true;

    try {
      mAddr = InetAddress.getByName("localhost");
    }
    catch (UnknownHostException e) {
      System.out.println("UnknownHostException : 'localhost'");
      e.printStackTrace();
      System.exit(-1);
    }

    try {
      mSocket = new DatagramSocket();
    }
    catch (SocketException e) {
      e.printStackTrace();
      System.exit(-1);
    }

    mSocket.connect(mAddr, mPort);
    mConnected = true;
  }

  /** Updates string metric value.
   * Inserts <code>value</code> into mValuesString map using a key that is
   * formulated as mPrefix + <code>name</code>.
   * @param name Name of metric
   * @param value Metric value
   */
  public void update(String name, String value) {
    mValuesString.put(mPrefix+name, value);
  }

  /** Updates short integer metric value.
   * Inserts <code>value</code> into mValuesInt map using a key that is
   * formulated as mPrefix + <code>name</code>.
   * @param name Name of metric
   * @param value Metric value
   */
  public void update(String name, short value) {
    mValuesInt.put(mPrefix+name, new Integer((int)value));
  }

  /** Updates integer metric value.
   * Inserts <code>value</code> into mValuesInt map using a key that is
   * formulated as mPrefix + <code>name</code>.
   * @param name Name of metric
   * @param value Metric value
   */
  public void update(String name, int value) {
    mValuesInt.put(mPrefix+name, new Integer(value));
  }

  /** Updates float metric value.
   * Inserts <code>value</code> into mValuesDouble map using a key that is
   * formulated as mPrefix + <code>name</code>.
   * @param name Name of metric
   * @param value Metric value
   */
  public void update(String name, float value) {
    mValuesDouble.put(mPrefix+name, new Double((double)value));
  }

  /** Updates double metric value.
   * Inserts <code>value</code> into mValuesDouble map using a key that is
   * formulated as mPrefix + <code>name</code>.
   * @param name Name of metric
   * @param value Metric value
   */
  public void update(String name, double value) {
    mValuesDouble.put(mPrefix+name, new Double(value));
  }

  /** Publishes metric values to Ganglia hypertable extension.
   * Constructs a JSON object containing the metrics key/value pairs constructed
   * from the mValuesString, mValuesInt, and mValuesDouble maps.  The JSON
   * string is sent to the the Ganglia hyperspace extension by sending it in the
   * form of a datagram packet over mSocket.
   * @throws Exception if the send fails
   */
  public void publish() throws Exception {

    if (mDisabled)
      return;

    if (!mConnected)
      mSocket.connect(mAddr, mPort);

    boolean first = true;
    String message = "{ ";

    for (Map.Entry<String, String> entry : mValuesString.entrySet()) {
      if (!first)
        message += ", \"";
      else {
        message += "\"";
        first = false;
      }
      message += entry.getKey() + "\": \"" + entry.getValue() + "\"";
    }

    for (Map.Entry<String, Integer> entry : mValuesInt.entrySet()) {
      if (!first)
        message += ", \"";
      else {
        message += "\"";
        first = false;
      }
      message += entry.getKey() + "\": " + entry.getValue();
    }

    for (Map.Entry<String, Double> entry : mValuesDouble.entrySet()) {
      if (!first)
        message += ", \"";
      else {
        message += "\"";
        first = false;
      }
      message += entry.getKey() + "\": " + entry.getValue();
    }

    message += " }";

    mConnected = false;
    byte [] sendData = message.getBytes("UTF-8");
    DatagramPacket sendPacket = new DatagramPacket(sendData, sendData.length, mAddr, mPort);
    mSocket.send(sendPacket);
    mConnected = true;

  }

  /** Metric name prefix ("ht." + component + ".") */
  private String mPrefix;

  /** Host address (localhost) of Ganglia gmond to connect to */
  private InetAddress mAddr;

  /** Ganglia hypertable extension listen port */
  private int mPort;

  /** Flag indicating if socket is connected */
  private boolean mConnected = false;

  /** Flag indicating if publishing is disabled */
  private boolean mDisabled = false;

  /** Datagram send socket */
  private DatagramSocket mSocket;

  /** Map holding string metric values */
  private HashMap<String, String> mValuesString = new HashMap<String, String>();

  /** Map holding integer metric values */
  private HashMap<String, Integer> mValuesInt = new HashMap<String, Integer>();

  /** Map holding floating point metric values */
  private HashMap<String, Double> mValuesDouble = new HashMap<String, Double>();
}
