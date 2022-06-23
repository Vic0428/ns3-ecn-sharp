import xml.etree.ElementTree as ET

DST_IP_ADDR = "10.1.4.32"
START_TIME = 172500
END_TIME = 172750

if __name__ == "__main__":
    tree = ET.parse("Large_Scale_undefined_9X4_DcTcp_0.25.xml")
    root = tree.getroot()
    for elem in list(root):
        if elem.tag == "Ipv4FlowClassifier":
            ipv4_flow_classifier = elem
        if elem.tag == "FlowStats":
            flow_stats = elem

    flow_elem_list = []
    flow_id_list = []
    for elem in list(ipv4_flow_classifier):
        attributes = elem.attrib
        if attributes["destinationAddress"] == DST_IP_ADDR:
            flow_elem_list.append(elem.attrib)
            flow_id_list.append(elem.attrib["flowId"])

    flow_id_list_in_time = []
    for elem in list(flow_stats) :
        this_flow_id = elem.attrib["flowId"]
        if this_flow_id in flow_id_list:
            t1 = float(elem.attrib["timeFirstTxPacket"][1:-2])
            t2 = float(elem.attrib["timeLastTxPacket"][1:-2])
            if t2 < START_TIME * 1000 or t1 > END_TIME * 1000:
                continue
            else:
                flow_id_list_in_time.append(this_flow_id)
                print(elem.attrib)

    for elem in flow_elem_list:
        if elem["flowId"] in flow_id_list_in_time:
            print(elem)
