const write_buffer = require('../db_apis/write_buffer.js');


function getWriteDataFromRec(req) {
  const writeData = {
    tag_id:     req.body.tag_id,
    data_value: req.body.data_value,
    username:   req.body.username,
    logtime:    req.body.logtime
  };
 
  return writeData;
}

async function post(req, res, next) {
  try {
    let writeData = getWriteDataFromRec(req);
    let rowCount = await write_buffer.writeValue(req);
    const retj = {};
	if(rowCount>0)
	{
		var utcSeconds = writeData.logtime;
		var d = new Date(0); // The 0 there is the key, which sets the date to the epoch
		d.setUTCSeconds(utcSeconds/1000);
		retj.DateTime = d;
		//retj.value = writeData.data_value;
		if(writeData.data_value==1)
		{
			retj.value = "Klima Açıldı";
		    retj.Success = true;
		    res.status(201).json(retj);
		}
		else if(writeData.data_value==2)
		{
			retj.value = "Klima Kapandı";
		    retj.Success = true;
		    res.status(201).json(retj);
		}
		else if(writeData.data_value==3)
		{
			retj.value = "Klima Otomatik";
			retj.Success = true;
		    res.status(201).json(retj);
		}
		else
		{
			retj.value = "Hatalı Veri";
			retj.Success = false;
		    res.status(500).json(retj);
		}
	}
	else{
		retj.Success = false;
		res.status(500).json(retj);
	}
  
  } catch (err) {
    next(err);
  }
}

module.exports.post = post;