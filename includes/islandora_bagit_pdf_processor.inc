<?php

/* 
 * This class is specific for newspaper module
 */

class IslandoraBagitPdfProcessor extends IslandoraBagitProcessor {
    
    function __construct($uri, $parent_collection, $pid_namespace, $recipe_data, $tmp_dir)
    {
        parent::__construct($uri, $parent_collection, $pid_namespace, $recipe_data, $tmp_dir);
    }
    
    function batchImportFromRecipeData(){
        $this->prepareImportDataFromBagit();
        $recipe_data = $this->getRecipeData();
        $pdf_collection = $this->updateIslandoraCollectionFromRecipeData($this->getParentCollection(), $recipe_data['recipe']);
        if(empty($pdf_collection)){
            drupal_set_message("Cannot add new PDF collection ".($recipe_data['recipe']['label']));
            throw new Exception("Cannot add new PDF collection ".($recipe_data['recipe']['label']));
        }
        $pdfs = $recipe_data['recipe']['pdfs'];
        
        $batch = array(
            'operations' => array(),
            'finished' => '_ou_bagit_importer_batches_finished',
            'title' => dt('Ingesting BAG...'),
            'init_message' => dt('Preparing to ingest BAG...'),
            'progress_message' => dt('Ingesting BAG'),
            'error_message' => dt('Page could not get ingested'),
        );
        
        // Every PDF object contains one PDF file so no loop here:
        foreach ($pdfs as $pdf_number => $pdf_data){
            $batch['operations'][] = array(
                'ou_bagit_importer_batch_import_page_by_processor',
                array(serialize($this), $pdf_collection->id, $pdf_data, $pdf_number+1),
            );
        }
        
        batch_set($batch);
        drupal_set_message(t('Page processing has been done.'));
    }
    
}
