<?php

/* 
 * This is the general class for handling the bagit for islandora repository
 */

abstract class IslandoraBagitProcessor
{
    static $islandora_content_models = array(
                                        "book"=>"islandora:bookCModel", 
                                        "newspaper"=>"islandora:newspaperCModel", 
                                        "newspaper_issue"=>"islandora:newspaperIssueCModel",
                                        "newspaper_page"=>"islandora:newspaperPageCModel",
                                        "book_page"=>"islandora:pageCModel",
                                        "audio"=>"islandora:sp-audioCModel"
                                    );
    // if a model has value true, it means it is a page type content model
    static $sequential_models = array(
        "islandora:newspaperIssueCModel" => false, "islandora:pageCModel" => true, "islandora:newspaperPageCModel" => true
    );
    
    var $uri;
    var $parent_collection;
    var $pid_namespace;
    var $recipe_data;
    var $tmp_dir;
    
    function __construct($uri, $parent_collection, $pid_namespace)
    {
        $this->uri = $uri;
        $this->parent_collection = $parent_collection;
        $this->pid_namespace = $pid_namespace;
    }
    
    function getUri(){
        return $this->uri;
    }
    
    function getParentCollection(){
        return $this->parent_collection;
    }
    
    function getPidNamespace(){
        return $this->pid_namespace;
    }
    
    function getRecipeData(){
        return $this->recipe_data;
    }
    
    function setRecipeData($recipe_data){
        $this->recipe_data = $recipe_data;
    }
    
    function getTmpDir(){
        return $this->tmp_dir;
    }
    
    function setTmpDir($tmp_dir){
        $this->tmp_dir = $tmp_dir;
    }
            
    function prepareImportDataFromBagit(){
        $file_data = drupal_json_decode(file_get_contents($this->uri));
        if (!$file_data) {
          drupal_set_message(t("Could not download recipe."));
          return;
        }
        $this->setRecipeData($file_data);
        $directory_basename = $file_data['recipe']['uuid'];
        $directory = drush_get_option('tmp_dir', "temporary://ou_bagit_importer/{$directory_basename}");
        if (file_prepare_directory($directory, FILE_CREATE_DIRECTORY)) {
            $this->setTmpDir($directory);
        }
    }
    
    function buildObjectModel($object_info) {
        
        $content_types = self::$islandora_content_models; 
        drupal_static_reset('islandora_get_tuque_connection');
        $tuque = islandora_get_tuque_connection();
        $uuid = $this->pid_namespace . ":" . $object_info['uuid'];
        $parent = $object_info['parent_collection'];
        $islandora_object = islandora_object_load($uuid);
        $update = (bool) $object_info['update'];
        $content_model = $content_types[$object_info['import']];

        $new = TRUE;
        if ($islandora_object && !$update) {
          drupal_set_message('Book page object exists');
          return array(
            'object' => $object_info['uuid'],
            'result' => FALSE,
            'message' => 'Book page object exists',
          );
        }
        elseif ($islandora_object) {
          $new = FALSE;
        }
        
        if (!$islandora_object) {
            $islandora_object = $tuque->repository->constructObject($uuid);
            $islandora_object->id = $uuid;
            $islandora_object->state = 'A';
            $islandora_object->label = $object_info['label'];
            $islandora_object->ownerId = $user->name;
            $islandora_object->relationships->add(FEDORA_MODEL_URI, 'hasModel', $content_model);
            $islandora_object->relationships->add(FEDORA_RELS_EXT_URI, 'isMemberOf', $parent);
            $this->retrieveObjectDatastreamsFromRecipeData($islandora_object, $object_info);
            if(array_key_exists($content_model, self::$sequential_models)){                
                if(self::$sequential_models[$content_model] === true){
                    $islandora_object->relationships->add(ISLANDORA_RELS_EXT_URI, 'isPageOf', $parent);                    
                    $islandora_object->relationships->add(ISLANDORA_RELS_EXT_URI, 'isPageNumber', $object_info['page_number']);
                    $islandora_object->relationships->add(ISLANDORA_RELS_EXT_URI, 'isSequenceNumber', $object_info['page_number']);
                    $islandora_object->relationships->add(ISLANDORA_RELS_EXT_URI, 'isSection', '1');
                }
                else{
                    $islandora_object->relationships->add(ISLANDORA_RELS_EXT_URI, 'isSequenceNumber', $object_info['sequence_number']);                    
                }
            }
            else{
                $islandora_object->relationships->add(FEDORA_MODEL_URI, 'hasModel', 'fedora-system:FedoraObject-3.0');
            }
        }
        else {
          $islandora_object->state = 'A';
          $islandora_object->label = $object_info['label'];
          $islandora_object->ownerId = $user->name;
        }
        
        if ($new) {
          islandora_add_object($islandora_object);
        }

        return $islandora_object;
    }
    
    private function retrieveObjectDatastreamsFromRecipeData(&$islandora_object, $recipe_data){
        
        if(!key_exists('metadata', $recipe_data) || empty($recipe_data['metadata'])){
            return;
        }
        
        $mods_record = ou_bagit_importer_run_xslt_transform(array(
            'input' => file_get_contents($recipe_data['metadata']['marcxml']),
            'xsl' => drupal_get_path('module', 'islandora_marcxml') . '/xsl/MARC21slim2MODS3-5.xsl',
        ));
        $dc_record = ou_bagit_importer_run_xslt_transform(array(
            'input' => $mods_record,
            'xsl' => drupal_get_path('module', 'islandora_batch') . '/transforms/mods_to_dc.xsl',
        ));
        $datastreams = array(
            'DC' => array(
              'type' => 'text/xml',
              'content' => $dc_record,
            ),
            'MODS' => array(
              'type' => 'text/xml',
              'content' => $mods_record,
            ),
        );
        if (!empty($recipe_data['metadata']['thumbnail'])) {
            $finfo = finfo_open(FILEINFO_MIME_TYPE);
            $datastreams['OBJ'] = array(
              'type' => finfo_file($finfo, drupal_realpath($recipe_data['metadata']['thumbnail'])),
              'content' => file_get_contents($recipe_data['metadata']['thumbnail']),
            );
            finfo_close($finfo);
        }
        
        foreach ($datastreams as $key => $value) {
            if (!$islandora_object[$key]) {
              $ds = $islandora_object->constructDatastream($key);
              $ds->label = $key;
              $ds->mimetype = $value['type'];
              $ds->control_group = 'M';
              $ds->setContentFromString($value['content']);
              $islandora_object->ingestDatastream($ds);
            }
            else {
                $ds = $islandora_object[$key];
                // Only update this datastream if it has changed.
                if (md5($ds->content) != md5($value['content'])) {
                  $ds->label = $key;
                  $ds->mimetype = $value['type'];
                  $ds->control_group = 'M';
                  $ds->setContentFromString($value['content']);
                }
            }
        }
    }


    /**
     * 
     * @param String $parent_collection
     * @param Array $recipe_data
     * @return Array $collection_obj
     */
    protected function updateIslandoraCollectionFromRecipeDataOld($parent_collection, $recipe_data) 
    {
        $object_array = $this->buildObjectModel($recipe_data);
        $is_update = $recipe_data['update'];
        drupal_static_reset('islandora_get_tuque_connection');
        $tuque = islandora_get_tuque_connection();
        $uuid = "{$this->pid_namespace}".":{$object_array['uuid']}";
        $islandora_object = islandora_object_load($uuid);
        $content_models = array(
          $object_array['type'],
          'fedora-system:FedoraObject-3.0',
        );
        $new = TRUE;

        // Don't touch already imported objects unless running an update.
        if ($islandora_object && !$is_update) {
          drupal_set_message('Collection object already exists');
          return array(
            'object' => $object_array['uuid'],
            'result' => FALSE,
            'message' => 'Collection object already exists',
          );
        }
        elseif ($islandora_object) {
          $new = FALSE;
          // We're running an update.
        }

        // Set up islandora object.
        if (!$islandora_object) {
          $islandora_object = $tuque->repository->constructObject( "{$object_array['pid_namespace']}". ":{$object_array['uuid']}");
          $islandora_object->id = "{$object_array['pid_namespace']}" . ":{$object_array['uuid']}";
          $islandora_object->state = 'A';
          $islandora_object->label = $object_array['label'];
          $islandora_object->ownerId = $user->name;
          foreach ($content_models as $content_model) {
            $islandora_object->relationships->add(FEDORA_MODEL_URI, 'hasModel', $content_model);
          }
          $islandora_object->relationships->add(FEDORA_RELS_EXT_URI, 'isMemberOfCollection', $parent_collection);
        }
        else {
          $islandora_object->state = 'A';
          $islandora_object->label = $object_array['label'];
          $islandora_object->ownerId = $user->name;
        }


        $mods_record = ou_bagit_importer_run_xslt_transform(array(
          'input' => file_get_contents($object_array['metadata']['marcxml']),
          'xsl' => drupal_get_path('module', 'islandora_marcxml') . '/xsl/MARC21slim2MODS3-5.xsl',
        ));

        $dc_record = ou_bagit_importer_run_xslt_transform(array(
          'input' => $mods_record,
          'xsl' => drupal_get_path('module', 'islandora_batch') . '/transforms/mods_to_dc.xsl',
        ));

        $datastreams = array(
          'DC' => array(
            'type' => 'text/xml',
            'content' => $dc_record,
          ),
          'MODS' => array(
            'type' => 'text/xml',
            'content' => $mods_record,
          ),
        );

        if (!empty($object_array['metadata']['thumbnail'])) {
          $finfo = finfo_open(FILEINFO_MIME_TYPE);
          $datastreams['OBJ'] = array(
            'type' => finfo_file($finfo, drupal_realpath($object_array['metadata']['thumbnail'])),
            'content' => file_get_contents($object_array['metadata']['thumbnail']),
          );
          finfo_close($finfo);
        }

        foreach ($datastreams as $key => $value) {

          if (!$islandora_object[$key]) {
            $ds = $islandora_object->constructDatastream($key);
            $ds->label = $key;
            $ds->mimetype = $value['type'];
            $ds->control_group = 'M';
            $ds->setContentFromString($value['content']);
            $islandora_object->ingestDatastream($ds);
          }
          else {
            $ds = $islandora_object[$key];
            // Only update this datastream if it has changed.
            if (md5($ds->content) != md5($value['content'])) {
              $ds->label = $key;
              $ds->mimetype = $value['type'];
              $ds->control_group = 'M';
              $ds->setContentFromString($value['content']);
            }
          }
        }

        if ($new) {
          islandora_add_object($islandora_object);
        }
        
        return array(
          'object' => $islandora_object->id,
          'result' => TRUE,
          'message' => 'Successfully ingested/updated',
        );
    }
    
    protected function updateIslandoraCollectionFromRecipeData($parent_collection, $object_info) {
        $object_info['parent_collection'] = $parent_collection;
        return $this->buildObjectModel($object_info);
    }
    
    function outputResults($result, $overwrite = FALSE){
        $directory = $this->tmp_dir;
        $results_file_path = "{$directory}/results.json"; 
        if ($overwrite && file_exists($results_file_path)) {
            $results = (array) drupal_json_decode(file_get_contents($results_file_path));
        }
        $results[$result['object']] = $result;
        file_put_contents($results_file_path, drupal_json_encode($results));
    }
    
    abstract protected function batchImportFromRecipeData();

//    abstract public function buildCollectionObject();
}
