let database;
const dbConfig = require('../config/database.js');

var isPgSql = true;
isPgSql = (dbConfig.hrPool.dbType=='POSTGRES');
database =  isPgSql ? require('../services/database_pg.js') : require('../services/database.js')

const baseQuery = 
 `select tag_id "id",
    tagname "tagname",
    kanal_id "kanal_id",
    polfreq "polfreq",
    deviceaddress "deviceaddress",
    variableaddress "variableaddress",
    variablesize "variablesize",
    variabletype "variabletype",
    islogging "islogging",
    privilegelevel "privilegelevel",
    tagtype "tagtype",
    schedulerindex "schedulerindex",
    loggingfreq "loggingfreq",
    loggingmethod "loggingmethod",
    readright1 "readright1",
    readright2 "readright2",
    writeright1 "writeright1",
    writeright2 "writeright2",
    keywords "keywords",
    enabled "enabled",
    description "description",
    unitname "unitname",
    protocolobjecttype "protocolobjecttype",
    toption "toption",
    logfilteroptions "logfilteroptions",
    equipment "equipment",
    measuredentity "measuredentity",
    location "location",
    formula "formula",
    modified "modified"
  from tags`;

async function find(context) {
  let query = baseQuery;
  const binds = {};
  const values = [];

  if (context.id) {
    binds.tag_id = context.id;
	values.push(context.id);
    query += isPgSql ? `\nwhere tag_id = $1` :`\nwhere tag_id = :tag_id`;
  }

   let result = isPgSql ? (await database.query(query,values)) :  (await database.simpleExecute(query, binds));


  return result.rows;
}

module.exports.find = find;