#include <mongolite.h>

SEXP R_mongo_collection_new(SEXP ptr_client, SEXP collection, SEXP db) {
  mongoc_client_t *client = r2client(ptr_client);
  mongoc_collection_t *col = mongoc_client_get_collection (client, translateCharUTF8(asChar(db)), translateCharUTF8(asChar(collection)));
  SEXP out = PROTECT(col2r(col));
  setAttrib(out, install("client"), ptr_client);
  UNPROTECT(1);
  return out;
}

SEXP R_mongo_get_default_database(SEXP ptr_client) {
  mongoc_client_t *client = r2client(ptr_client);
  mongoc_database_t *db = mongoc_client_get_default_database(client);
  if(db){
    return mkString(mongoc_database_get_name(db));
  } else {
    return R_NilValue;
  }
}

SEXP R_mongo_collection_drop (SEXP ptr){
  mongoc_collection_t *col = r2col(ptr);
  bson_error_t err;

  if(!mongoc_collection_drop(col, &err))
    stop(err.message);

  return ScalarLogical(1);
}

SEXP R_mongo_collection_name (SEXP ptr){
  mongoc_collection_t *col = r2col(ptr);
  const char * name = mongoc_collection_get_name(col);
  return mkStringUTF8(name);
}

SEXP R_mongo_collection_count (SEXP ptr, SEXP ptr_query){
  mongoc_collection_t *col = r2col(ptr);
  bson_t *query = r2bson(ptr_query);

  bson_error_t err;
  int64_t count = mongoc_collection_count (col, MONGOC_QUERY_NONE, query, 0, 0, NULL, &err);
  if (count < 0)
    stop(err.message);

  //R does not support int64
  return ScalarReal((double) count);
}

SEXP R_mongo_collection_insert_bson(SEXP ptr_col, SEXP ptr_bson, SEXP stop_on_error){
  mongoc_collection_t *col = r2col(ptr_col);
  bson_t *b = r2bson(ptr_bson);
  mongoc_insert_flags_t flags = asLogical(stop_on_error) ? MONGOC_INSERT_NONE : MONGOC_INSERT_CONTINUE_ON_ERROR;
  bson_error_t err;

  if(!mongoc_collection_insert(col, flags, b, NULL, &err))
    stop(err.message);

  return ScalarLogical(1);
}

SEXP R_mongo_collection_update(SEXP ptr_col, SEXP ptr_selector, SEXP ptr_update, SEXP upsert, SEXP multiple){
  mongoc_collection_t *col = r2col(ptr_col);
  bson_t *selector = r2bson(ptr_selector);
  bson_t *update = r2bson(ptr_update);

  //set update flags
  mongoc_update_flags_t flags = MONGOC_UPDATE_NONE;
  if(asLogical(upsert))
    flags = flags + MONGOC_UPDATE_UPSERT;
  if(asLogical(multiple))
    flags = flags + MONGOC_UPDATE_MULTI_UPDATE;

  bson_error_t err;
  if(!mongoc_collection_update(col, flags, selector, update, NULL, &err))
    stop(err.message);

  return ScalarLogical(1);
}

SEXP R_mongo_collection_insert_page(SEXP ptr_col, SEXP json_vec, SEXP stop_on_error){
  if(!isString(json_vec) || !length(json_vec))
    stop("json_vec must be character string of at least length 1");

  //ordered means serial execution
  bool ordered = asLogical(stop_on_error);

  //create bulk operation
  bson_error_t err;
  bson_t *b;
  bson_t reply;
  mongoc_bulk_operation_t *bulk = mongoc_collection_create_bulk_operation (r2col(ptr_col), ordered, NULL);
  for(int i = 0; i < length(json_vec); i++){
    b = bson_new_from_json ((uint8_t*)translateCharUTF8(asChar(STRING_ELT(json_vec, i))), -1, &err);
    if(!b){
      mongoc_bulk_operation_destroy (bulk);
      stop(err.message);
    }
    mongoc_bulk_operation_insert(bulk, b);
    bson_destroy (b);
    b = NULL;
  }

  //execute bulk operation
  bool success = mongoc_bulk_operation_execute (bulk, &reply, &err);
  mongoc_bulk_operation_destroy (bulk);

  //check for errors
  if(!success){
    if(ordered){
      Rf_errorcall(R_NilValue, err.message);
    } else {
      Rf_warningcall(R_NilValue, "Not all inserts were successful: %s\n", err.message);
    }
  }

  //get output
  SEXP out = PROTECT(bson2list(&reply));
  bson_destroy (&reply);
  UNPROTECT(1);
  return out;
}

SEXP R_mongo_collection_create_index(SEXP ptr_col, SEXP ptr_bson) {
  mongoc_collection_t *col = r2col(ptr_col);
  bson_t *b = r2bson(ptr_bson);
  const mongoc_index_opt_t *options = mongoc_index_opt_get_default();
  bson_error_t err;

  if(!mongoc_collection_create_index(col, b, options, &err))
    stop(err.message);

  return ScalarLogical(1);
}

SEXP R_mongo_collection_drop_index(SEXP ptr_col, SEXP name) {
  mongoc_collection_t *col = r2col(ptr_col);
  const char *str = translateCharUTF8(asChar(name));
  bson_error_t err;

  if(!mongoc_collection_drop_index(col, str, &err))
    stop(err.message);

  return ScalarLogical(1);
}

SEXP R_mongo_collection_remove(SEXP ptr_col, SEXP ptr_bson, SEXP all){
  mongoc_collection_t *col = r2col(ptr_col);
  bson_t *b = r2bson(ptr_bson);
  bson_error_t err;
  mongoc_remove_flags_t flags = asLogical(all) ? MONGOC_REMOVE_NONE : MONGOC_REMOVE_SINGLE_REMOVE;

  if(!mongoc_collection_remove(col, flags, b, NULL, &err))
    stop(err.message);

  return ScalarLogical(1);
}

SEXP R_mongo_collection_find(SEXP ptr_col, SEXP ptr_query, SEXP ptr_fields, SEXP skip, SEXP limit) {
  mongoc_collection_t *col = r2col(ptr_col);
  bson_t *query = r2bson(ptr_query);
  bson_t *fields = r2bson(ptr_fields);
  mongoc_query_flags_t flags = MONGOC_QUERY_NONE;

  mongoc_cursor_t *c = mongoc_collection_find(col, flags, asInteger(skip), asInteger(limit),
    0, query, fields, NULL);

  return cursor2r(c);
}

SEXP R_mongo_collection_command_simple(SEXP ptr_col, SEXP command){
  mongoc_collection_t *col = r2col(ptr_col);
  bson_t *cmd = r2bson(command);
  bson_t reply;
  bson_error_t err;
  if(!mongoc_collection_command_simple(col, cmd, NULL, &reply, &err))
    stop(err.message);

  SEXP out = PROTECT(bson2list(&reply));
  bson_destroy (&reply);
  UNPROTECT(1);
  return out;
}

SEXP R_mongo_collection_stats(SEXP ptr_col){
  mongoc_collection_t *col = r2col(ptr_col);
  bson_t reply;
  bson_error_t err;
  if(!mongoc_collection_stats(col, NULL, &reply, &err))
    stop(err.message);

  SEXP out = PROTECT(bson2list(&reply));
  bson_destroy (&reply);
  UNPROTECT(1);
  return out;
}

SEXP R_mongo_collection_find_indexes(SEXP ptr_col) {
  mongoc_collection_t *col = r2col(ptr_col);
  bson_error_t err;

  mongoc_cursor_t *c = mongoc_collection_find_indexes (col, &err);
  if(!c)
    stop(err.message);

  return cursor2r(c);
}

SEXP R_mongo_collection_rename(SEXP ptr_col, SEXP db, SEXP name) {
  mongoc_collection_t *col = r2col(ptr_col);
  bson_error_t err;
  const char *new_db = NULL;
  if(db != R_NilValue)
    new_db = translateCharUTF8(asChar(db));

  if(!mongoc_collection_rename(col, new_db, translateCharUTF8(asChar(name)), false, &err))
    stop(err.message);
  return ScalarLogical(1);
}

SEXP R_mongo_collection_aggregate(SEXP ptr_col, SEXP ptr_pipeline) {
  mongoc_collection_t *col = r2col(ptr_col);
  bson_t *pipeline = r2bson(ptr_pipeline);
  mongoc_cursor_t *c = mongoc_collection_aggregate (col, MONGOC_QUERY_NONE, pipeline, NULL, NULL);
  if(!c)
    stop("Error executing pipeline.");
  return cursor2r(c);
}

SEXP R_mongo_collection_command(SEXP ptr_col, SEXP ptr_cmd){
  mongoc_collection_t *col = r2col(ptr_col);
  bson_t *cmd = r2bson(ptr_cmd);
  mongoc_cursor_t *c = mongoc_collection_command(col, MONGOC_QUERY_NONE, 0, 0, 0, cmd, NULL, NULL);
  if(!c)
    stop("Error executing command.");
  return cursor2r(c);
}
