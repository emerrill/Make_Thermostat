<?php
//Settings
$arduinoKey = "akey"; //Match to REMOTE_PASSWORD in Arduino Sketch
$webPassword = "thermostat";

//DB Setup
$user = "make";
$password = "maker";
$database = "make_remote";
$host = "localhost";










mysql_connect($host,$user,$password);

@mysql_select_db($database) or die( "Unable to select database");

//Make tables if they don't exist
$sql = "SHOW TABLES LIKE 'historicData'";
$result = get_record_sql($sql);
if (!$result) { //Make table
    $sql = "CREATE TABLE `historicData` (
  `id` int(11) NOT NULL auto_increment,
  `temperature` float default NULL,
  `humidity` float default NULL,
  `mode` tinyint(4) default NULL,
  `targetTemp` float default NULL,
  `holdMode` int(11) default NULL,
  `tempTime` int(11) default NULL,
  `remoteTime` int(11) default NULL,
  `serverTime` int(11) NOT NULL default '0',
  PRIMARY KEY  (`id`)
) ENGINE=MyISAM AUTO_INCREMENT=573 DEFAULT CHARSET=latin1 AUTO_INCREMENT=573 ;";

    mysql_query($sql);
}

$sql = "SHOW TABLES LIKE 'config'";
$result = get_record_sql($sql);
if (!$result) { //Make table
    $sql = "CREATE TABLE `config` (
  `id` int(11) NOT NULL auto_increment,
  `name` varchar(128) NOT NULL default '',
  `value` text NOT NULL,
  `needsSending` tinyint(4) NOT NULL default '0',
  PRIMARY KEY  (`id`)
) ENGINE=MyISAM AUTO_INCREMENT=18 DEFAULT CHARSET=latin1 AUTO_INCREMENT=18 ;";
    
    mysql_query($sql);
}

date_default_timezone_set('UTC');


if ($_SERVER['HTTP_USER_AGENT'] == "Arduino") {
    $response = '!!RES!!?';

    $data = array_merge($_GET, $_POST);
    
    if ((!isset($data['p'])) || ($data['p'] != $arduinoKey)) {
        print "!!RES!!?error=error";
    }
    
    $stats = new stdClass();
    $stats->serverTime = time();

    if (isset($data['temp'])) {
        $stats->temperature = $data['temp'];
    }
    
    if (isset($data['hum'])) {
        $stats->humidity = $data['hum'];
    }
    
    if (isset($data['ta'])) {
        $targetTemp = get_record('config', 'name', 'targetTemp');
        if ($targetTemp) {
            if (!$targetTemp->needsSending) {
                $targetTemp->value = $data['ta'];
                update_record('config', $targetTemp);
            }
        } else {
            $targetTemp = new stdClass();
            $targetTemp->name = 'targetTemp';
            $targetTemp->value = $data['ta'];
            $targetTemp->needsSending = 0;
            
            insert_record('config', $targetTemp);
        }
    
        $stats->targetTemp = $data['ta'];
    }
    
    if (isset($data['time'])) {
        $stats->remoteTime = $data['time'];
    }
    
    if (isset($data['hold'])) {
        $holdMode = get_record('config', 'name', 'holdMode');
        if ($holdMode) {
            if (!$holdMode->needsSending) {
                $holdMode->value = $data['hold'];
                update_record('config', $holdMode);
            }
        } else {
            $holdMode = new stdClass();
            $holdMode->name = 'holdMode';
            $holdMode->value = $data['hold'];
            $holdMode->needsSending = 0;
            
            insert_record('config', $holdMode);
        }
            
    
        $stats->holdMode = $data['hold'];
    }
    
    if (isset($data['tt'])) {
        $stats->holdMode = $data['tt'];
    }
    
    if (isset($data['mode'])) {
        $mode = get_record('config', 'name', 'HVACMode');
        if ($mode) {
            if (!$mode->needsSending) {
                $mode->value = $data['mode'];
                update_record('config', $mode);
            }
        } else {
            $mode = new stdClass();
            $mode->name = 'HVACMode';
            $mode->value = $data['mode'];
            $mode->needsSending = 0;
            
            insert_record('config', $mode);
        }
    
        $stats->mode = $data['mode'];
    }

    insert_record('historicData', $stats);




    if (isset($data['ts']) && isset($data['tc'])) {
        $timeSlots = (int)$data['ts'];
        $timesChange = (int)$data['tc'];
        $existing = get_record('config', 'name', 'timeTemps');
        if (!$existing || ($timesChange)) {
            $timeTemps = array('times' => array(), 'temps' => array());
            for ($i = 0; $i < $timeSlots; $i++) {
                if (isset($data['times'.$i]) && isset($data['temps'.$i])) {
                    $timeTemps['times'][$i] = $data['times'.$i];
                    $timeTemps['temps'][$i] = $data['temps'.$i];
                }
            }
            
            
            $obj = new stdClass();
            $obj->name = 'timeTemps';
            $obj->value = serialize($timeTemps);
            $obj->needsSending = 0;
            if ($existing) {
            print "existing";
                $obj->id = $existing->id;
                update_record('config', $obj);
            } else {
                insert_record('config', $obj);
            }
        }
        
    }
    
    $timeTempsObj = get_record('config', 'name', 'timeTemps');
    
    if ($timeTempsObj && ($timeTempsObj->needsSending)) {
        $timeTemps = unserialize($timeTempsObj->value);
        $count = count($timeTemps['times']);
        $response .= 'ts='.$count.'&';
        
        for ($i = 0; $i < $count; $i++) {
            $response .= 'times'.$i.'='.$timeTemps['times'][$i].'&';
            $response .= 'temps'.$i.'='.$timeTemps['temps'][$i].'&';
        }
        
        $timeTempsObj->needsSending = 0;
        update_record('config', $timeTempsObj);
    }
    
    $offsetRec = get_record('config', 'name', 'timeoffset');
    if ($offsetRec && ($offsetRec->needsSending)) {
        $response .= 'setTime='.(time()+($offsetRec->value*60)).'&';
        
        $offsetRec->needsSending = 0;
        update_record('config', $offsetRec);
    }
    
    $holdMode = get_record('config', 'name', 'holdMode');
    if ($holdMode && $holdMode->needsSending) {
        $response .= 'hold='.$holdMode->value.'&';
        
        $holdMode->needsSending = 0;
        update_record('config', $holdMode);
    }
    
    $hvacMode = get_record('config', 'name', 'HVACMode');
    if ($hvacMode && $hvacMode->needsSending) {
        $response .= 'mode='.$hvacMode->value.'&';
        
        $hvacMode->needsSending = 0;
        update_record('config', $hvacMode);
    }
    
    $targetTemp = get_record('config', 'name', 'targetTemp');
    if ($targetTemp && $targetTemp->needsSending) {
        $response .= 'ta='.$targetTemp->value.'&';
        
        $targetTemp->needsSending = 0;
        update_record('config', $targetTemp);
    }
    
    
    
	
	print $response.' ';
} else {
    session_start();
    
    if (isset($_POST['login'])) {
        if ($_POST['pass'] == $webPassword) {
            $_SESSION['thermostat_authed'] = 1;
        } 
    }
    
    if ($_GET['page'] == 'logout') {
        unset($_SESSION['thermostat_authed']);
        print '<meta http-equiv="Refresh" content="0; URL='.$_SERVER['SCRIPT_NAME'].'">';
        exit;
    }
    
    if (!isset($_SESSION['thermostat_authed']) || !$_SESSION['thermostat_authed']) {
        
        
        print '<html><form method="POST">';
        
        print 'Password: <input type=password name=pass><br><br>';
        
        print '<input name=login value=Login type=submit>';
    	print '</form></html>';
        exit;
    }

    $page = $_GET['page'];
    
    
    print '<html><head>
        <style type="text/css">
        a:visited{color:blue}
        </style>
    
    ';
    
    if ($page == 'history') {
        $rows = get_records_sql('SELECT * FROM historicData ORDER BY serverTime DESC LIMIT 600');

        $data = array();
        
        foreach ($rows as $row) {
            $line = "['".date('H:i:s n/j/Y', $row->serverTime)."', ".$row->targetTemp.", ".$row->temperature.", ".$row->humidity."]";
            $data[] = $line;
        }
        
        $dataString = implode(',', $data);

    
        print '
                <script type="text/javascript" src="https://www.google.com/jsapi"></script>
                <script type="text/javascript">
                  google.load("visualization", "1", {packages:["corechart"]});
                  google.setOnLoadCallback(drawChart);
                  function drawChart() {
                    var data = new google.visualization.DataTable();
                    data.addColumn(\'string\', \'Time\');
                    data.addColumn(\'number\', \'Target \u00B0F\');
                    data.addColumn(\'number\', \'Temperature \u00B0F\');
                    data.addColumn(\'number\', \'Humidity %\');
                    data.addRows(['.$dataString.']);
            
                    var options = {
                      width: 800, height: 500,
                      title: \'Thermostat History\'
                    };
            
                    var chart = new google.visualization.LineChart(document.getElementById(\'chart_div\'));
                    chart.draw(data, options);
                  }
                </script>
              </head><body>';
    } else {
        print '</head><body><form method="POST">';
    }
    
    
    print '<a href="?page=main">Main</a> <a href="?page=time">Time</a> <a href="?page=timeSlots">Time Slots</a> <a href="?page=history">History</a> <a href="?page=logout">Logout</a><br>';
    
    switch ($page) {
        case "time":
            if (isset($_POST['submit']) && is_numeric($_POST['offset'])) {
                $offsetRec = get_record('config', 'name', 'timeoffset');
                
                if (!$offsetRec) {
                    $offsetRec = new stdClass();
                    $offsetRec->name = 'timeoffset';
                }
                
                $offsetRec->value = $_POST['offset'];
                $offsetRec->needsSending = 1;
                
                
                if (isset($offsetRec->id)) {
                    update_record('config', $offsetRec);
                } else {
                    insert_record('config', $offsetRec);
                }
            }
        
            $offsetRec = get_record('config', 'name', 'timeoffset');
            
            if ($offsetRec) {
                $offset = '' + $offsetRec->value;
            } else {
                $offset = '0';
            }
            
            print 'Server time is: '.date('H:i:s n/j/Y').' GMT<br>';
            
            $lastData = get_record_sql('SELECT * FROM historicData ORDER BY serverTime DESC LIMIT 1');
            if ($lastData) {
                print 'Last thermostat time was: '.date('H:i:s n/j/Y', $lastData->remoteTime).' Local<br>';
            } else {
                print 'No data received from thermostat.<br>';
            }
            
            print 'Time offset: <input type=text name="offset" value="'.$offset.'"> minutes.<br>';
            print 'Time difference from server reported GMT time, to thermostat local time.<br>EST offset is normally -300 (-5 hours times 60)<br><br>Submitting will update the thermostat, to server time plus offset, at next checkin.<br><br>';
            
            break;
            
        case "history": 
            
            print '<div id="chart_div"></div>';
            break;
            
        case "timeSlots":
            if (isset($_POST['submit'])) {
                $timeSlots = min($_POST['slotsCount'], 24);
                $existing = get_record('config', 'name', 'timeTemps');
                $timeTemps = array('times' => array(), 'temps' => array());
                for ($i = 0; $i < $timeSlots; $i++) {
                    if (isset($_POST['times'.$i]) && isset($_POST['temps'.$i])) {
                        $timeTemps['times'][$i] = $_POST['times'.$i];
                        $timeTemps['temps'][$i] = $_POST['temps'.$i];
                    }
                }
                
                
                $obj = new stdClass();
                $obj->name = 'timeTemps';
                $obj->value = serialize($timeTemps);
                $obj->needsSending = 1;
                if ($existing) {
                    $obj->id = $existing->id;
                    update_record('config', $obj);
                } else {
                    insert_record('config', $obj);
                }
            }
        
            $timeTemps = get_record('config', 'name', 'timeTemps');
            if (!$timeTemps) {
                print "No data synced from thermostat<br>";
                break;
            }
            $timeTemps = unserialize($timeTemps->value);
            
            $count = count($timeTemps['times']);
            print '<input type=hidden name=slotsCount value='.$count.'>';
            for ($i = 0; $i < $count; $i++) {
                print 'Time:<input type=text name="times'.$i.'" value="'.$timeTemps['times'][$i].'">';
                print ' Temp:<input type=text name="temps'.$i.'" value="'.$timeTemps['temps'][$i].'"><br>';
            }
            break;
        
        
        case 'main':
        default:
            $lastData = get_record_sql('SELECT * FROM historicData ORDER BY serverTime DESC LIMIT 1');
            $mode = get_record('config', 'name', 'HVACMode');
            $holdMode = get_record('config', 'name', 'holdMode');
            $targetTemp = get_record('config', 'name', 'targetTemp');

            if (isset($_POST['submit'])) {
                $mode = get_record('config', 'name', 'HVACMode');
                $holdMode = get_record('config', 'name', 'holdMode');
                $targetTemp = get_record('config', 'name', 'targetTemp');
                
                if (isset($_POST['modes']) && is_numeric($_POST['modes'])) {
                    if ($mode->value != $_POST['modes']) {
                        $mode->value = $_POST['modes'];
                        $mode->needsSending = 1;
                        
                        update_record('config', $mode);
                    }
                }
                
                $allowProgramMode = true;
                if (isset($_POST['targetTemp']) && is_numeric($_POST['targetTemp'])) {
                    if ($targetTemp->value != $_POST['targetTemp']) {
                        $allowProgramMode = false;
                        $targetTemp->value = $_POST['targetTemp'];
                        $targetTemp->needsSending = 1;
                        
                        update_record('config', $targetTemp);
                    }
                }
                
                if ((isset($_POST['holdModes']) && is_numeric($_POST['holdModes'])) || (($allowProgramMode == false) && ($holdMode->value == 0))) {
                    if (($holdMode->value != $_POST['holdModes']) || (($allowProgramMode == false) && ($_POST['holdModes'] == 0))) {
                        $holdMode->value = $_POST['holdModes'];
                        $holdMode->needsSending = 1;
                        
                        if (($allowProgramMode == false) && ($holdMode->value == 0)) {
                            $holdMode->value = 1;
                        }
                        
                        update_record('config', $holdMode);
                    }
                }
            }
            
            
            print 'At '.date('H:i:s n/j/Y', $lastData->remoteTime).'<br>';
            print 'Temperature: <font size="+2"><b>'.$lastData->temperature.'</b>&deg;F</font><br>';
            print 'Relative Humidity: <font size="+1">'.$lastData->humidity.'%</font><br><br>';
            print 'HVAC Mode: <select name=modes>';
            print '<option value=0 '.($mode->value == 0 ? 'selected' : '').'>Off</option>';
            print '<option value=1 '.($mode->value == 1 ? 'selected' : '').'>Heat</option>';
            print '<option value=2 '.($mode->value == 2 ? 'selected' : '').'>Cool</option></select><br><br>';
            
            
            print 'Temperature Target: <input type=text name=targetTemp value="'.$targetTemp->value.'">&deg;F<br>';
            print 'Temperature Mode: <select name=holdModes>';
            print '<option value=0 '.($holdMode->value == 0 ? 'selected' : '').'>Program</option>';
            print '<option value=1 '.($holdMode->value == 1 ? 'selected' : '').'>Temporary Hold</option>';
            print '<option value=2 '.($holdMode->value == 2 ? 'selected' : '').'>Perminant Hold</option></select><br><br>';
        
            break;
    }
    
    if ($page != 'history') {
        print '<input name=submit value=Save type=submit>';
    	
    	print '</form>';
	}
	
	print '</body></html>';
	


}

mysql_close();






function get_record($table, $field1, $value1, $field2='', $value2='', $fields='*')
{

    $sql = 'SELECT '.$fields.' FROM '.$table .' WHERE '. $field1 .' = \''. $value1 .'\'';

    if ($field2) {
        $sql .= ' AND '. $field2 .' = \''. $value2 .'\'';
    }
    
    return get_record_sql($sql);

}

function get_record_sql($sql)
{

    
    $result = mysql_query($sql);

    if ($result) {
        return mysql_fetch_object($result);
    } else {
        return false;
    }
    
}

function get_records_sql($sql)
{
    $output = array();
    
    $result = mysql_query($sql);

    if ($result) {
        while ($obj = mysql_fetch_object($result)) {
            $output[] = $obj;
        }
        return $output;
    } else {
        return false;
    }
    
}


function insert_record($table, &$obj) {
    if (isset($obj->id)) {
        return false;
    }
    
    $objArray = (array)$obj;

    $keys = array();
    $vals = array();
    foreach ($objArray as $key => $val) {
        $keys[] = mysql_real_escape_string($key);
        $vals[] = '\''.mysql_real_escape_string($val).'\'';
    }
    
    $sql = 'INSERT INTO '.$table .' ('.implode(',', $keys).') VALUES ('.implode(',', $vals).')';
    
    mysql_query($sql);
}

function update_record($table, &$obj) {
    if (!isset($obj->id) || !is_numeric($obj->id)) {
        return false;
    }
    
    $objArray = (array)$obj;
    
    $id = $objArray['id'];
    unset($objArray['id']);
    
    $keys = array();
    $vals = array();
    $subsql = '';
    foreach ($objArray as $key => $val) {
        $subsql .= mysql_real_escape_string($key).' = \''.mysql_real_escape_string($val).'\', ';
    }
    $subsql = substr($subsql, 0, -2);
    
    $sql = 'UPDATE '.$table .' SET '.$subsql.' WHERE id = '.$id;
    
    mysql_query($sql);
}



?>