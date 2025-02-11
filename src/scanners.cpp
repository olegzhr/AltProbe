/*
 *   Copyright 2021 Oleg Zharkov
 *
 *   Licensed under the Apache License, Version 2.0 (the "License").
 *   You may not use this file except in compliance with the License.
 *   A copy of the License is located at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   or in the "license" file accompanying this file. This file is distributed
 *   on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *   express or implied. See the License for the specific language governing
 *   permissions and limitations under the License.
 */

#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <list>
#include <vector>
#include <memory>
#include <sstream>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <iostream>

#include "scanners.h"

#define SOCKET_BUFFER_SIZE 2048

namespace bpt = boost::property_tree;

int Scanners::GetConfig() {
       
    update_status = 1;
    return update_status;
}

int Scanners::Open(int mode, pid_t pid) {
    
    bool amq_conn = false;
    int conn_attempts = 0;
    
    altprobe_mode = mode;
    p_pid = pid;
    
    do {
        try {
            if (connection == NULL) {
                
                activemq::library::ActiveMQCPP::initializeLibrary();
                
                if (ssl_broker) {
                    
                    decaf::lang::System::setProperty( "decaf.net.ssl.trustStore", cert );
                    
                    if (ssl_verify) {
                        decaf::lang::System::setProperty("decaf.net.ssl.disablePeerVerification", "false");
                    } else {
                        decaf::lang::System::setProperty("decaf.net.ssl.disablePeerVerification", "true");
                    }
                    
                } 
                
                if (ssl_client) {
                    decaf::lang::System::setProperty("decaf.net.ssl.keyStore", key); 
                    decaf::lang::System::setProperty("decaf.net.ssl.keyStorePassword", key_pwd); 
                } 
                
                // Create a ConnectionFactory
                string strUrl(url);
            
                unique_ptr<ConnectionFactory> connectionFactory(
                    ConnectionFactory::createCMSConnectionFactory(strUrl));
            
                // Create a Connection
                if (user_pwd) {
                    connection = connectionFactory->createConnection(user,pwd);
                } else {
                    connection = connectionFactory->createConnection();
                }
                
                connection->start();
            }
            
            if (session == NULL) {
                // Create a Session
                if (this->sessionTransacted) {
                    session = connection->createSession(Session::SESSION_TRANSACTED);
                } else {
                    session = connection->createSession(Session::AUTO_ACKNOWLEDGE);
                }
            }
            
            string ref_id;
            
            if(project_id.compare(fs.filter.ref_id) && project_id.compare("indef")) {
                ref_id = project_id;
            } else {
                ref_id = fs.filter.ref_id;
            }
            
            // Create the MessageConsumer
            string strConsumer("jms/altprobe/" + node_id + "/" + host_name + "/scanners");
            
            Destination* consumerCommand = session->createQueue(strConsumer);
            
            // Create a MessageConsumer from the Session to the Topic or Queue
            consumer = session->createConsumer(consumerCommand);
 
            consumer->setMessageListener(this);
            
            mq_counter++;
        
            amq_conn = true;
            
            string log = "listens scanners bus";
            SysLog((char*) log.c_str());
 
        } catch (CMSException& e) {
        
            if (conn_attempts > 10) {
                SysLog("activeMQ operation error");
                string str = e.getMessage();
                const char * c = str.c_str();
                SysLog((char*) c);
                return 0;
            }
            sleep(3);
            conn_attempts++;
        }
        
    } while (!amq_conn);
    
    return 1;
}

int Scanners::Go(void) {
    
    sleep(1);
        
    return 1;
}

// Called from the consumer since this class is a registered MessageListener.
void Scanners::onMessage(const Message* message) {
    
    try {
        
        string corr_id = message->getCMSCorrelationID();
        string headerJson = "{ \"request_id\": \"" +  corr_id + "\", ";
        string bodyJson = "\"status\": 400 }";        
        
        if (dynamic_cast<const TextMessage*> (message)) {
            bodyJson = onTextMessage(message, corr_id);
        } else {
            SysLog("ActiveMQ CMS Exception occurred: scanners module");
            CheckStatus();
            return;
        }
        
        string responseJson = headerJson + bodyJson;
        
        // Create a MessageProducer from the Session to Queue
        const Destination* tmpDest = message->getCMSReplyTo();
        MessageProducer* tmpProd = session->createProducer(tmpDest);
            
        auto_ptr<TextMessage> response(session->createTextMessage(responseJson));
        tmpProd->send(response.get());
            
        delete tmpProd;
        tmpProd = NULL;
        
    } catch (CMSException& e) {
        SysLog("ActiveMQ CMS Exception occurred: scanners module");
        CheckStatus();
        return;
    }
 
    if (this->sessionTransacted) {
        session->commit();
    }
}
 
// If something bad happens you see it here as this class is also been
// registered as an ExceptionListener with the connection.
void Scanners::onException(const CMSException& ex AMQCPP_UNUSED) {
    SysLog("ActiveMQ CMS Exception occurred: scanners module");
    CheckStatus();
}

void Scanners::Close() {
    
    // Destroy resources.
    try {
        if (consumer) {
            delete consumer;
            consumer = NULL;
        }
        
        m_controller.lock();
        mq_counter--;
        m_controller.unlock();
        
        if (mq_counter == 0) {
            
            delete session;
            session = NULL;
            
            delete connection;
            connection = NULL;
        }
        
    } catch (CMSException& e) {
        SysLog("activeMQ operation error: destroy resources");
    }
}

string Scanners::onTextMessage(const Message* message, string corr_id) {
    
    if(!rcStatus) return "\"status\": 400, \"status_text\": \"remote control function is disabled\" }";
    
    const TextMessage* textMessage = dynamic_cast<const TextMessage*> (message);
                
    string c2json = textMessage->getText();
    
    //************************************************************************************************************************
    SysLog((char*) c2json.c_str());
    
    stringstream c2json_ss(c2json);
    bpt::ptree pt;
    bpt::read_json(c2json_ss, pt);
    
    string project =  pt.get<string>("actuator.x-alertflex.project","indef");
    
    if(project.compare(fs.filter.ref_id) && project.compare(project_id)) {
        
        return "\"status\": 400, \"status_text\": \"wrong project\" }"; 
    }
    
    string target = pt.get<string>("target.device.device_id","indef");
    
    if(!target.compare("indef")) {
    
        return "\"status\": 400, \"status_text\": \"wrong target\" }"; 
    } 
    
    string action =  pt.get<string>("action","indef");
    
    if(action.compare("scan")) { 
        
        return "\"status\": 400, \"status_text\": \"wrong action\" }";
    }
    
    int delay = pt.get<int>("args.delay",0);
    
    int posture_type =  pt.get<int>("args.posture_type",0);
    
    string alert_corr =  pt.get<string>("args.alert_corr","indef");
    
    // run trivy    
    if(posture_type > 3 && posture_type < 14) {
                    
        try {
                
            string res =  ScanTrivy(project, target, delay, posture_type, alert_corr, corr_id);
                        
            return res;
            
        } catch (const std::exception & ex) {
            
            return "\"status\": 400, \"status_text\": \"wrong response\" }"; 
        }
    }
    
    
    if(posture_type == 14) {
        
        try {
                
            string res =  ScanKubeHunter(project, target, delay, alert_corr, corr_id);
                        
            return res;
            
        } catch (const std::exception & ex) {
            return "\"status\": 400, \"status_text\": \"wrong response\" }"; 
        }
    }
    
    if(posture_type == 15) {
        
        try {
                
            string res =  ScanZap(project, target, delay, alert_corr, corr_id);
                        
            return res;
            
        } catch (const std::exception & ex) {
            return "\"status\": 400, \"status_text\": \"wrong response\" }"; 
        }
    }
    
    if(posture_type == 16) {
        
        try {
                
            string res =  ScanNmap(project, target, delay, alert_corr, corr_id);
                        
            return res;
            
        } catch (const std::exception & ex) {
            return "\"status\": 400, \"status_text\": \"wrong response\" }"; 
        }
    }
    
    if(posture_type == 17) {
        
        try {
                
            string res =  ScanNuclei(project, target, delay, alert_corr, corr_id);
                        
            return res;
            
        } catch (const std::exception & ex) {
            return "\"status\": 400, \"status_text\": \"wrong response\" }"; 
        }
    }
    
    if(posture_type == 18) {
        
        try {
                
            string res =  ScanNikto(project, target, delay, alert_corr, corr_id);
                        
            return res;
            
        } catch (const std::exception & ex) {
            return "\"status\": 400, \"status_text\": \"wrong response\" }"; 
        }
    }
    
    if(posture_type == 19) {
        
        try {
                
            string res =  ScanCloudsploit(project, target, delay, alert_corr, corr_id);
                        
            return res;
            
        } catch (const std::exception & ex) {
            return "\"status\": 400, \"status_text\": \"wrong response\" }"; 
        }
    }
    
    if(posture_type == 20) {
        
        try {
                
            string res =  ScanSemgrep(project, target, delay, alert_corr, corr_id);
                        
            return res;
            
        } catch (const std::exception & ex) {
            return "\"status\": 400, \"status_text\": \"wrong response\" }"; 
        }
    }
    
    if(posture_type == 21) {
        
        try {
                
            string res =  ScanSonarqube(project, target, delay, alert_corr, corr_id);
                        
            return res;
            
        } catch (const std::exception & ex) {
            return "\"status\": 400, \"status_text\": \"wrong response\" }"; 
        }
    }
    
    return "\"status\": 400, \"status_text\": \"wrong actuator or action\" }";
}


string Scanners::ScanTrivy(string project, string target, int delay, int param, string alert_corr, string corr_id) {
    
    try {
        
        string trivy_path_str(trivy_path);
        string result_path_str(result_path);
        string trivy_result = result_path_str + "/trivy.json";
        
        string cmd;
        
        switch (param) {
            
            case 4: // appSecret
                cmd = trivy_path_str + "/trivy fs --security-checks secret -f json -o " + trivy_result + " " + target;
                SysLog((char*) cmd.c_str());
                break;
            
            case 5: // dockerConfig
                cmd = trivy_path_str + "/trivy fs --security-checks config -f json -o " + trivy_result + " " + target;
                SysLog((char*) cmd.c_str());
                break;
                
            case 6: // k8sConfig
                cmd = trivy_path_str + "/trivy k8s --security-checks config -f json -o " + trivy_result + " cluster";
                SysLog((char*) cmd.c_str());
                break;
                
            case 7: // appVuln
                cmd = trivy_path_str + "/trivy fs --security-checks vuln -f json -o " + trivy_result + " " + target;
                SysLog((char*) cmd.c_str());
                break;
                
            case 8: // dockerVuln
                cmd = trivy_path_str + "/trivy image --security-checks vuln -f json -o " + trivy_result + " " + target;
                SysLog((char*) cmd.c_str());
                break;
                
            case 9: // k8sVuln
                cmd = trivy_path_str + "/trivy k8s --security-checks vuln -f json -o " + trivy_result + " cluster";
                SysLog((char*) cmd.c_str());
                break;
                
            case 10: // appSbom
                cmd = trivy_path_str + "/trivy fs --format cyclonedx --output " + trivy_result + " " + target;
                SysLog((char*) cmd.c_str());
                break;
                
            case 11: // dockerSbom
                cmd = trivy_path_str + "/trivy image --format cyclonedx --output " + trivy_result + " " + target;
                SysLog((char*) cmd.c_str());
                break;
                
            case 12: // cloudFormation
                cmd = trivy_path_str + "/trivy fs --security-checks config -f json -o " + trivy_result + " " + target;
                SysLog((char*) cmd.c_str());
                break;
                
            case 13: // terraform
                cmd = trivy_path_str + "/trivy fs --security-checks config -f json -o " + trivy_result + " " + target;
                SysLog((char*) cmd.c_str());
                break;
                
            default:
                return "trivy: error";
        }
        
        system(cmd.c_str());
        
        std::ifstream trivy_report;
        
        trivy_report.open(trivy_result,ios::binary);
        strStream << trivy_report.rdbuf();
        
        boost::iostreams::filtering_streambuf< boost::iostreams::input> in;
        in.push(boost::iostreams::gzip_compressor());
        in.push(strStream);
        boost::iostreams::copy(in, comp);
        
        posture.event_type = param;
        posture.data = comp.str();
        posture.ref_id = project;
        posture.target = target;
        posture.uuid = corr_id;
        posture.alert_corr = alert_corr;
        SendMessage(&posture);
                
        trivy_report.close();
        boost::iostreams::close(in);
        ResetStreams();
        
    } catch (const std::exception & ex) {
        SysLog((char*) ex.what());
        return "trivy: error";
    } 
    
    return "\"status\": 200 }";
}

string Scanners::ScanKubeHunter(string project, string target, int delay, string alert_corr, string corr_id) {
    
    try {
        
        string kubehunter_path_str(kubehunter_path);
        string result_path_str(result_path);
        string kubehunter_result = result_path_str + "/kube-hunter.json";
                
        string cmd =  "/etc/altprobe/scripts/kube-hunter.sh " + result_path_str + " " + kubehunter_path_str + " " + target;
        SysLog((char*) cmd.c_str());
        system(cmd.c_str());
        
        std::ifstream kubehunter_report;
        
        kubehunter_report.open(kubehunter_result,ios::binary);
        strStream << kubehunter_report.rdbuf();
        
        boost::iostreams::filtering_streambuf< boost::iostreams::input> in;
        in.push(boost::iostreams::gzip_compressor());
        in.push(strStream);
        boost::iostreams::copy(in, comp);
        
        posture.event_type = 14;
        posture.data = comp.str();
        posture.ref_id = project;
        posture.target = target;
        posture.uuid = corr_id;
        posture.alert_corr = alert_corr;
        SendMessage(&posture);
                
        kubehunter_report.close();
        boost::iostreams::close(in);
        ResetStreams();
        
    } catch (const std::exception & ex) {
        SysLog((char*) ex.what());
        return "kube-hunter: error";
    } 
    
    return "\"status\": 200 }";
}

string Scanners::ScanZap(string project, string target, int delay, string alert_corr, string corr_id) {
    
    try {
        
        string zap_path_str(zap_path);
        string result_path_str(result_path);
        string zap_result = result_path_str + "/zap.json";
        
        string cmd = "/etc/altprobe/scripts/zap.sh " + result_path_str + " " + zap_path_str + " " + target;
        SysLog((char*) cmd.c_str());
        system(cmd.c_str());
        
        std::ifstream zap_report;
        
        zap_report.open(zap_result,ios::binary);
        strStream << zap_report.rdbuf();
        
        boost::iostreams::filtering_streambuf< boost::iostreams::input> in;
        in.push(boost::iostreams::gzip_compressor());
        in.push(strStream);
        boost::iostreams::copy(in, comp);
        
        posture.event_type = 15;
        posture.data = comp.str();
        posture.ref_id = project;
        posture.target = target;
        posture.uuid = corr_id;
        posture.alert_corr = alert_corr;
        SendMessage(&posture);
                
        zap_report.close();
        boost::iostreams::close(in);
        ResetStreams();
        
    } catch (const std::exception & ex) {
        SysLog((char*) ex.what());
        return "zap: error";
    } 
    
    return "\"status\": 200 }";
}

string Scanners::ScanNmap(string project, string target, int delay, string alert_corr, string corr_id) {
    
    try {
        
        string nmap_path_str(nmap_path);
        string result_path_str(result_path);
        string nmap_result = result_path_str + "/nmap.xml";
        
        string cmd = "/etc/altprobe/scripts/nmap.sh " + result_path_str + " " + nmap_path_str + " " + target;
        SysLog((char*) cmd.c_str());
        system(cmd.c_str());
        
        std::ifstream nmap_report;
        
        nmap_report.open(nmap_result,ios::binary);
        strStream << nmap_report.rdbuf();
        
        boost::iostreams::filtering_streambuf< boost::iostreams::input> in;
        in.push(boost::iostreams::gzip_compressor());
        in.push(strStream);
        boost::iostreams::copy(in, comp);
        
        posture.event_type = 16;
        posture.data = comp.str();
        posture.ref_id = project;
        posture.target = target;
        posture.uuid = corr_id;
        posture.alert_corr = alert_corr;
        SendMessage(&posture);
                
        nmap_report.close();
        boost::iostreams::close(in);
        ResetStreams();
        
    } catch (const std::exception & ex) {
        SysLog((char*) ex.what());
        return "nmap: error";
    } 
    
    return "\"status\": 200 }";
}


string Scanners::ScanNuclei(string project, string target, int delay, string alert_corr, string corr_id) {
    
    try {
        
        string nuclei_path_str(nuclei_path);
        string result_path_str(result_path);
        string nuclei_result = result_path_str + "/nuclei.json";
        
        string cmd = "/etc/altprobe/scripts/nuclei.sh " + result_path_str + " " + nuclei_path_str + " " + target;
        SysLog((char*) cmd.c_str());
        system(cmd.c_str());
        
        std::ifstream nuclei_report;
        
        nuclei_report.open(nuclei_result,ios::binary);
        strStream << nuclei_report.rdbuf();
        
        boost::iostreams::filtering_streambuf< boost::iostreams::input> in;
        in.push(boost::iostreams::gzip_compressor());
        in.push(strStream);
        boost::iostreams::copy(in, comp);
        
        posture.event_type = 17;
        posture.data = comp.str();
        posture.ref_id = project;
        posture.target = target;
        posture.uuid = corr_id;
        posture.alert_corr = alert_corr;
        SendMessage(&posture);
                
        nuclei_report.close();
        boost::iostreams::close(in);
        ResetStreams();
        
    } catch (const std::exception & ex) {
        SysLog((char*) ex.what());
        return "nuclei: error";
    } 
    
    return "\"status\": 200 }";
}


string Scanners::ScanNikto(string project, string target, int delay, string alert_corr, string corr_id) {
    
    try {
        
        string nikto_path_str(nikto_path);
        string result_path_str(result_path);
        string nikto_result = result_path_str + "/nikto.json";
        
        string cmd = "/etc/altprobe/scripts/nikto.sh " + result_path_str + " " + nikto_path_str + " " + target;
        SysLog((char*) cmd.c_str());
        system(cmd.c_str());
        
        std::ifstream nikto_report;
        
        nikto_report.open(nikto_result,ios::binary);
        strStream << nikto_report.rdbuf();
        
        boost::iostreams::filtering_streambuf< boost::iostreams::input> in;
        in.push(boost::iostreams::gzip_compressor());
        in.push(strStream);
        boost::iostreams::copy(in, comp);
        
        posture.event_type = 18;
        posture.data = comp.str();
        posture.ref_id = project;
        posture.target = target;
        posture.uuid = corr_id;
        posture.alert_corr = alert_corr;
        SendMessage(&posture);
                
        nikto_report.close();
        boost::iostreams::close(in);
        ResetStreams();
        
    } catch (const std::exception & ex) {
        SysLog((char*) ex.what());
        return "nikto: error";
    } 
    
    return "\"status\": 200 }";
}

string Scanners::ScanCloudsploit(string project, string target, int delay, string alert_corr, string corr_id) {
    
    try {
        
        string cloudsploit_path_str(cloudsploit_path);
        string result_path_str(result_path);
        string cloudsploit_result = result_path_str + "/cloudsploit.json";
        
        string cmd = "/etc/altprobe/scripts/cloudsploit.sh " + result_path_str + " " + cloudsploit_path_str;
        SysLog((char*) cmd.c_str());
        system(cmd.c_str());
        
        std::ifstream cloudsploit_report;
        
        cloudsploit_report.open(cloudsploit_result,ios::binary);
        strStream << cloudsploit_report.rdbuf();
        
        boost::iostreams::filtering_streambuf< boost::iostreams::input> in;
        in.push(boost::iostreams::gzip_compressor());
        in.push(strStream);
        boost::iostreams::copy(in, comp);
        
        posture.event_type = 19;
        posture.data = comp.str();
        posture.ref_id = project;
        posture.target = target;
        posture.uuid = corr_id;
        posture.alert_corr = alert_corr;
        SendMessage(&posture);
                
        cloudsploit_report.close();
        boost::iostreams::close(in);
        ResetStreams();
        
    } catch (const std::exception & ex) {
        SysLog((char*) ex.what());
        return "cloudsploit: error";
    } 
    
    return "\"status\": 200 }";
}

string Scanners::ScanSemgrep(string project, string target, int delay, string alert_corr, string corr_id) {
    
    try {
        
        string semgrep_path_str(semgrep_path);
        string result_path_str(result_path);
        string semgrep_result = result_path_str + "/semgrep.json";
        
        string cmd = "/etc/altprobe/scripts/semgrep.sh " + result_path_str + " " + semgrep_path_str + " " + target;
        SysLog((char*) cmd.c_str());
        system(cmd.c_str());
        
        std::ifstream semgrep_report;
        
        semgrep_report.open(semgrep_result,ios::binary);
        strStream << semgrep_report.rdbuf();
        
        boost::iostreams::filtering_streambuf< boost::iostreams::input> in;
        in.push(boost::iostreams::gzip_compressor());
        in.push(strStream);
        boost::iostreams::copy(in, comp);
        
        posture.event_type = 20;
        posture.data = comp.str();
        posture.ref_id = project;
        posture.target = target;
        posture.uuid = corr_id;
        posture.alert_corr = alert_corr;
        SendMessage(&posture);
                
        semgrep_report.close();
        boost::iostreams::close(in);
        ResetStreams();
        
    } catch (const std::exception & ex) {
        SysLog((char*) ex.what());
        return "semgrep: error";
    } 
    
    return "\"status\": 200 }";
}

string Scanners::ScanSonarqube(string project, string target, int delay, string alert_corr, string corr_id) {
    
    try {
        
        string sonarqube_path_str(sonarqube_path);
                
        string cmd = "/etc/altprobe/scripts/sonarqube.sh " + sonarqube_path_str + " " + target;
        SysLog((char*) cmd.c_str());
        system(cmd.c_str());
        
        posture.event_type = 21;
        posture.data = "no data";
        posture.ref_id = project;
        posture.target = target;
        posture.uuid = corr_id;
        posture.alert_corr = alert_corr;
        SendMessage(&posture);
                
    } catch (const std::exception & ex) {
        SysLog((char*) ex.what());
        return "sonarqube: error";
    } 
    
    return "\"status\": 200 }";
}


